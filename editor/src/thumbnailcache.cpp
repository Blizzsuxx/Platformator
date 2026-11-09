#include "thumbnailcache.h"

#include "imagepreflight.h"
#include "editorwork.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace platformator_editor
{
ThumbnailCache::ThumbnailCache() : ownerThread_(std::this_thread::get_id())
{
}

ThumbnailCache::Result::~Result()
{
    if (surface != nullptr)
        SDL_DestroySurface(surface);
}

ThumbnailCache::Result::Result(Result &&other) noexcept
    : request(std::move(other.request)), surface(std::exchange(other.surface, nullptr)),
      sourceWidth(other.sourceWidth), sourceHeight(other.sourceHeight), byteCost(other.byteCost),
      message(std::move(other.message))
{
}

ThumbnailCache::Result &ThumbnailCache::Result::operator=(Result &&other) noexcept
{
    if (this == &other)
        return *this;
    if (surface != nullptr)
        SDL_DestroySurface(surface);
    request = std::move(other.request);
    surface = std::exchange(other.surface, nullptr);
    sourceWidth = other.sourceWidth;
    sourceHeight = other.sourceHeight;
    byteCost = other.byteCost;
    message = std::move(other.message);
    return *this;
}

ThumbnailCache::~ThumbnailCache()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_.clear();
    }
    tasks_.wait();
}

std::string ThumbnailCache::key_for(const std::filesystem::path &path)
{
    return path.lexically_normal().string();
}

bool ThumbnailCache::owner_thread_ok() const noexcept
{
    if (std::this_thread::get_id() == ownerThread_)
        return true;
    std::lock_guard lock(mutex_);
    ownerThreadViolation_ = true;
    return false;
}

void ThumbnailCache::enqueue_locked(const std::filesystem::path &path,
                                    const std::string &key, Entry &entry)
{
    if (stopping_ || !entry.stale || entry.failed)
        return;
    const auto same_request = [&](const Request &request) {
        return request.key == key && request.revision == entry.revision &&
               request.cacheGeneration == cacheGeneration_;
    };
    if ((runningRequest_ && same_request(*runningRequest_)) ||
        (completed_ && same_request(completed_->request)) ||
        std::any_of(pending_.begin(), pending_.end(), same_request))
        return;
    const std::size_t outstanding = pending_.size() + (running_ ? 1U : 0U) +
                                    (completed_ ? 1U : 0U);
    if (outstanding >= ThumbnailMaxPendingPaths)
        return;
    pending_.push_back({path, key, entry.revision, cacheGeneration_});
    start_next_locked();
}

void ThumbnailCache::enqueue_replica_locked(const std::string &key, SDL_Renderer *renderer,
                                             const Entry &entry)
{
    if (renderer == nullptr || entry.pixels == nullptr || stopping_ ||
        entry.replicas.contains(renderer))
        return;
    const auto duplicate = std::any_of(pendingReplicas_.begin(), pendingReplicas_.end(),
        [&](const ReplicaRequest &request) {
            return request.key == key && request.renderer == renderer &&
                   request.revision == entry.revision &&
                   request.cacheGeneration == cacheGeneration_;
        });
    if (!duplicate && pendingReplicas_.size() < ThumbnailMaxPendingReplicas)
        pendingReplicas_.push_back({key, renderer, entry.revision, cacheGeneration_});
}

void ThumbnailCache::start_next_locked()
{
    if (stopping_ || running_ || completed_ || pending_.empty())
        return;
    Request request = std::move(pending_.front());
    pending_.pop_front();
    const auto afterPreparationTestHook = afterPreparationTestHook_;
    running_ = true;
    runningRequest_ = request;
    tasks_.run([this, request, afterPreparationTestHook] {
        Result result;
        result.request = request;
        try
        {
            work::record(work::Counter::ImageDecodes);
            auto prepared = prepare_image(request.path);
            if (!prepared.ready())
                result.message = prepared.preflight.message;
            else
            {
                result.sourceWidth = prepared.surface->w;
                result.sourceHeight = prepared.surface->h;
                const double scale = std::min({1.0,
                    static_cast<double>(ThumbnailMaxDimension) / result.sourceWidth,
                    static_cast<double>(ThumbnailMaxDimension) / result.sourceHeight});
                const int width = std::max(1, static_cast<int>(std::lround(result.sourceWidth * scale)));
                const int height = std::max(1, static_cast<int>(std::lround(result.sourceHeight * scale)));
                SDL_Surface *scaled = SDL_ScaleSurface(prepared.surface.get(), width, height,
                                                       SDL_SCALEMODE_LINEAR);
                if (scaled == nullptr)
                    result.message = std::string("Thumbnail scaling failed: ") + SDL_GetError();
                else
                {
                    result.surface = scaled;
                    result.byteCost = static_cast<std::uint64_t>(scaled->w) *
                                      static_cast<std::uint64_t>(scaled->h) * 4ULL;
                }
            }
        }
        catch (const std::exception &error)
        {
            result.message = error.what();
        }
        catch (...)
        {
            result.message = "Unknown thumbnail preparation failure.";
        }
        if (afterPreparationTestHook)
        {
            try { afterPreparationTestHook(request.path); }
            catch (...) {}
        }
        std::function<void()> wake;
        {
            std::lock_guard lock(mutex_);
            running_ = false;
            runningRequest_.reset();
            if (!stopping_ && request.cacheGeneration == cacheGeneration_ && result.surface != nullptr &&
                result.byteCost <= ThumbnailMaxCompletedBytes &&
                result.byteCost <= ThumbnailMaxOutputSurfaceBytes)
            {
                completedBytes_ = result.byteCost;
                completed_.emplace(std::move(result));
            }
            else if (!stopping_ && request.cacheGeneration == cacheGeneration_ &&
                     result.surface != nullptr)
            {
                const auto entry = entries_.find(request.key);
                if (entry != entries_.end() && entry->second.revision == request.revision)
                {
                    entry->second.failed = true;
                    entry->second.message = "Thumbnail output exceeded the 256-pixel cache surface limit.";
                }
            }
            else if (!stopping_ && request.cacheGeneration == cacheGeneration_ && !result.message.empty())
            {
                const auto entry = entries_.find(request.key);
                if (entry != entries_.end() && entry->second.revision == request.revision)
                {
                    entry->second.failed = true;
                    entry->second.message = std::move(result.message);
                }
            }
            start_next_locked();
            wake = wakeCallback_;
        }
        if (wake)
        {
            try { wake(); }
            catch (...) {}
        }
    });
}

SDL_Texture *ThumbnailCache::request(const std::filesystem::path &path, SDL_Renderer *renderer)
{
    if (!owner_thread_ok())
        return nullptr;
    const auto key = key_for(path);
    SDL_Renderer *targetRenderer = renderer;
    std::lock_guard lock(mutex_);
    if (targetRenderer == nullptr)
        targetRenderer = ownerRenderer_;
    auto found = entries_.find(key);
    bool inserted = false;
    if (found == entries_.end())
    {
        if (!make_room_for_locked(0, key))
            return nullptr;
        found = entries_.try_emplace(key).first;
        inserted = true;
    }
    Entry &entry = found->second;
    entry.demandedThisFrame = true;
    if (inserted)
    {
        entry.stale = true;
        entry.revision = 1;
    }
    SDL_Texture *texture = nullptr;
    if (targetRenderer != nullptr)
    {
        const auto replica = entry.replicas.find(targetRenderer);
        if (replica != entry.replicas.end())
        {
            texture = replica->second.texture;
            entry.lastUse = ++logicalClock_;
            framePins_.insert(key);
        }
    }
    if (entry.stale && !entry.failed)
        enqueue_locked(path, key, entry);
    else if (texture == nullptr && entry.pixels != nullptr)
        enqueue_replica_locked(key, targetRenderer, entry);
    return texture;
}

SDL_Texture *ThumbnailCache::find(const std::filesystem::path &path,
                                  SDL_Renderer *renderer) const noexcept
{
    if (!owner_thread_ok())
        return nullptr;
    const auto key = key_for(path);
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end())
        return nullptr;
    SDL_Renderer *targetRenderer = renderer != nullptr ? renderer : ownerRenderer_;
    if (targetRenderer == nullptr)
        return nullptr;
    const auto replica = found->second.replicas.find(targetRenderer);
    return replica == found->second.replicas.end() ? nullptr : replica->second.texture;
}

bool ThumbnailCache::stale(const std::filesystem::path &path) const noexcept
{
    const auto key = key_for(path);
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    return found != entries_.end() && found->second.stale;
}

std::pair<int, int> ThumbnailCache::source_dimensions(const std::filesystem::path &path) const noexcept
{
    const auto key = key_for(path);
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end())
        return {0, 0};
    return {found->second.sourceWidth, found->second.sourceHeight};
}

std::pair<float, float> ThumbnailCache::source_to_thumbnail_scale(
    const std::filesystem::path &path, SDL_Renderer *renderer) const noexcept
{
    if (!owner_thread_ok())
        return {1.0f, 1.0f};
    const auto key = key_for(path);
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end() ||
        found->second.sourceWidth <= 0 || found->second.sourceHeight <= 0)
        return {1.0f, 1.0f};
    SDL_Renderer *targetRenderer = renderer != nullptr ? renderer : ownerRenderer_;
    if (targetRenderer == nullptr)
        return {1.0f, 1.0f};
    const auto replica = found->second.replicas.find(targetRenderer);
    if (replica == found->second.replicas.end() || replica->second.texture == nullptr)
        return {1.0f, 1.0f};
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    if (!SDL_GetTextureSize(replica->second.texture, &textureWidth, &textureHeight) ||
        textureWidth <= 0.0f || textureHeight <= 0.0f)
        return {1.0f, 1.0f};
    return {textureWidth / static_cast<float>(found->second.sourceWidth),
            textureHeight / static_cast<float>(found->second.sourceHeight)};
}

std::string ThumbnailCache::failure_message(const std::filesystem::path &path) const
{
    const auto key = key_for(path);
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end() && entries_.size() >= ThumbnailMaxEntries)
        return "Thumbnail cache capacity is full while visible entries are pinned.";
    return found == entries_.end() ? std::string{} : found->second.message;
}

bool ThumbnailCache::make_room_for_locked(std::uint64_t bytes, const std::string &protectedKey)
{
    if (bytes > ThumbnailMaxRetainedBytes)
        return false;
    const auto outputReservation = completedBytes_ + uploadingBytes_;
    if (outputReservation > ThumbnailMaxRetainedBytes ||
        bytes > ThumbnailMaxRetainedBytes - outputReservation)
        return false;
    const auto textureLimit = ThumbnailMaxRetainedBytes - outputReservation - bytes;
    const bool protectedEntryExists = entries_.find(protectedKey) != entries_.end();
    while ((!protectedEntryExists && entries_.size() >= ThumbnailMaxEntries) ||
           retainedBytes_ > textureLimit)
    {
        auto victim = entries_.end();
        for (auto candidate = entries_.begin(); candidate != entries_.end(); ++candidate)
        {
            if (candidate->first == protectedKey || framePins_.contains(candidate->first))
                continue;
            if (victim == entries_.end() || candidate->second.lastUse < victim->second.lastUse)
                victim = candidate;
        }
        if (victim == entries_.end())
            return false;
        for (const auto &[replicaRenderer, replica] : victim->second.replicas)
        {
            (void)replicaRenderer;
            if (replica.texture != nullptr)
                SDL_DestroyTexture(replica.texture);
        }
        if (victim->second.pixels != nullptr)
            SDL_DestroySurface(victim->second.pixels);
        retainedBytes_ -= victim->second.bytes;
        const auto evictedKey = victim->first;
        entries_.erase(victim);
        std::erase_if(pending_, [&](const Request &request) { return request.key == evictedKey; });
        std::erase_if(pendingReplicas_, [&](const ReplicaRequest &request) {
            return request.key == evictedKey;
        });
    }
    return true;
}

void ThumbnailCache::poll_uploads(SDL_Renderer *renderer)
{
    if (!owner_thread_ok() || renderer == nullptr)
        return;
    std::optional<Result> result;
    std::optional<ReplicaRequest> replicaRequest;
    SDL_Surface *replicaPixels = nullptr;
    std::uint64_t replicaBytes = 0;
    {
        std::lock_guard lock(mutex_);
        if (ownerRenderer_ == nullptr)
            ownerRenderer_ = renderer;
        if (completed_ && completed_->request.cacheGeneration == cacheGeneration_)
        {
            result.emplace(std::move(*completed_));
            completed_.reset();
            uploadingBytes_ = result->byteCost;
            completedBytes_ = 0;
        }
        else
        {
            for (auto pending = pendingReplicas_.begin(); pending != pendingReplicas_.end();)
            {
                const auto entry = entries_.find(pending->key);
                const bool current = pending->cacheGeneration == cacheGeneration_ &&
                    entry != entries_.end() && entry->second.revision == pending->revision &&
                    entry->second.pixels != nullptr;
                if (!current)
                {
                    pending = pendingReplicas_.erase(pending);
                    continue;
                }
                if (pending->renderer != renderer)
                {
                    ++pending;
                    continue;
                }
                replicaRequest.emplace(*pending);
                replicaPixels = entry->second.pixels;
                replicaBytes = entry->second.pixelBytes;
                pendingReplicas_.erase(pending);
                uploadingBytes_ = replicaBytes;
                if (!make_room_for_locked(0, replicaRequest->key))
                {
                    entry->second.message = "Thumbnail cache is full while visible images are pinned.";
                    replicaRequest.reset();
                    replicaPixels = nullptr;
                    uploadingBytes_ = 0;
                }
                break;
            }
            if (!replicaRequest)
                start_next_locked();
        }
    }

    if (result)
    {
        const auto bytes = static_cast<std::uint64_t>(result->surface->w) *
                           static_cast<std::uint64_t>(result->surface->h) * 4ULL;
        bool current = false;
        bool budgetAvailable = false;
        {
            std::lock_guard lock(mutex_);
            auto entry = entries_.find(result->request.key);
            current = result->request.cacheGeneration == cacheGeneration_ &&
                      entry != entries_.end() && entry->second.revision == result->request.revision;
            if (current)
            {
                // Reserve both retained copies before allocating the renderer-local
                // texture. Existing replicas stay valid until replacement succeeds.
                uploadingBytes_ = result->byteCost + bytes;
                budgetAvailable = make_room_for_locked(0, result->request.key);
                if (!budgetAvailable)
                {
                    entry->second.failed = true;
                    entry->second.message = "Thumbnail cache is full while visible images are pinned.";
                }
            }
            if (!current || !budgetAvailable)
            {
                uploadingBytes_ = 0;
                start_next_locked();
            }
        }
        if (!current || !budgetAvailable)
        {
            result.reset();
            return;
        }

        work::record(work::Counter::TextureUploads);
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, result->surface);
        SDL_Surface *previousPixels = nullptr;
        std::vector<SDL_Texture *> previousTextures;
        bool published = false;
        {
            std::lock_guard publishLock(mutex_);
            auto entry = entries_.find(result->request.key);
            const bool stillCurrent = result->request.cacheGeneration == cacheGeneration_ &&
                entry != entries_.end() && entry->second.revision == result->request.revision;
            if (stillCurrent && texture == nullptr)
            {
                entry->second.failed = true;
                entry->second.message = std::string("Thumbnail upload failed: ") + SDL_GetError();
            }
            else if (stillCurrent)
            {
                auto &cached = entry->second;
                previousPixels = cached.pixels;
                for (const auto &[previousRenderer, replica] : cached.replicas)
                {
                    (void)previousRenderer;
                    if (replica.texture != nullptr)
                        previousTextures.push_back(replica.texture);
                }
                retainedBytes_ -= cached.bytes;
                cached.pixels = result->surface;
                result->surface = nullptr;
                cached.pixelBytes = bytes;
                cached.replicas.clear();
                cached.replicas.emplace(renderer, Entry::Replica{texture, bytes});
                cached.bytes = bytes * 2;
                cached.sourceWidth = result->sourceWidth;
                cached.sourceHeight = result->sourceHeight;
                cached.lastUse = ++logicalClock_;
                cached.stale = false;
                cached.failed = false;
                cached.message.clear();
                retainedBytes_ += cached.bytes;
                published = true;
            }
        }
        if (!published && texture != nullptr)
            SDL_DestroyTexture(texture);
        if (previousPixels != nullptr)
            SDL_DestroySurface(previousPixels);
        for (SDL_Texture *previous : previousTextures)
            SDL_DestroyTexture(previous);
        result.reset();
        std::lock_guard finishLock(mutex_);
        uploadingBytes_ = 0;
        start_next_locked();
        return;
    }

    if (!replicaRequest)
    {
        std::lock_guard lock(mutex_);
        start_next_locked();
        return;
    }

    work::record(work::Counter::TextureUploads);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, replicaPixels);
    bool published = false;
    {
        std::lock_guard lock(mutex_);
        const auto entry = entries_.find(replicaRequest->key);
        const bool current = replicaRequest->cacheGeneration == cacheGeneration_ &&
            entry != entries_.end() && entry->second.revision == replicaRequest->revision &&
            entry->second.pixels == replicaPixels;
        if (current && texture != nullptr)
        {
            const auto [replica, inserted] = entry->second.replicas.emplace(
                renderer, Entry::Replica{texture, replicaBytes});
            (void)replica;
            if (inserted)
            {
                entry->second.bytes += replicaBytes;
                entry->second.lastUse = ++logicalClock_;
                retainedBytes_ += replicaBytes;
                published = true;
            }
        }
        else if (current)
            entry->second.message = std::string("Thumbnail upload failed: ") + SDL_GetError();
    }
    if (!published && texture != nullptr)
        SDL_DestroyTexture(texture);
    std::lock_guard finishLock(mutex_);
    uploadingBytes_ = 0;
    start_next_locked();
}

void ThumbnailCache::set_wake_callback(std::function<void()> callback)
{
    std::lock_guard lock(mutex_);
    wakeCallback_ = std::move(callback);
}

void ThumbnailCache::invalidate(const std::filesystem::path &path, bool subtree,
                                std::uint64_t eventSequence)
{
    const auto normalized = path.lexically_normal();
    std::lock_guard lock(mutex_);
    const auto matches = [&](const std::filesystem::path &candidate) {
        const auto relative = candidate.lexically_relative(normalized);
        const bool insideSubtree = subtree && !relative.empty() && !relative.is_absolute() &&
                                   *relative.begin() != "..";
        return candidate == normalized || insideSubtree;
    };
    for (auto &[key, entry] : entries_)
    {
        const std::filesystem::path candidate(key);
        if (!matches(candidate))
            continue;
        // One event may be delivered through multiple logical aliases. Suppress
        // only that event's duplicate delivery; a newer sequence always creates
        // a new revision, even while preparation is already stale/in flight.
        if (eventSequence != 0 && entry.lastInvalidationSequence == eventSequence)
            continue;
        entry.stale = true;
        entry.failed = false;
        entry.message.clear();
        entry.lastInvalidationSequence = eventSequence;
        ++entry.revision;
        if (entry.revision == 0)
            ++entry.revision;
    }
    for (auto request = pending_.begin(); request != pending_.end();)
    {
        const auto candidate = std::filesystem::path(request->key);
        if (matches(candidate))
            request = pending_.erase(request);
        else
            ++request;
    }
    for (auto &[key, entry] : entries_)
    {
        const std::filesystem::path candidate(key);
        if (matches(candidate) && (entry.demandedThisFrame || entry.demandedPreviousFrame))
            enqueue_locked(candidate, key, entry);
    }
}

void ThumbnailCache::clear(SDL_Renderer *renderer) noexcept
{
    if (!owner_thread_ok())
        return;
    std::lock_guard lock(mutex_);
    ++cacheGeneration_;
    if (cacheGeneration_ == 0)
        ++cacheGeneration_;
    pending_.clear();
    completed_.reset();
    completedBytes_ = 0;
    uploadingBytes_ = 0;
    framePins_.clear();
    for (auto &[key, entry] : entries_)
    {
        (void)key;
        for (const auto &[replicaRenderer, replica] : entry.replicas)
        {
            (void)replicaRenderer;
            if (replica.texture != nullptr)
                SDL_DestroyTexture(replica.texture);
        }
        if (entry.pixels != nullptr)
            SDL_DestroySurface(entry.pixels);
    }
    entries_.clear();
    retainedBytes_ = 0;
    pendingReplicas_.clear();
    ownerRenderer_ = renderer;
}

void ThumbnailCache::release_renderer(SDL_Renderer *renderer) noexcept
{
    if (!owner_thread_ok() || renderer == nullptr)
        return;
    std::lock_guard lock(mutex_);
    for (auto &[key, entry] : entries_)
    {
        (void)key;
        const auto replica = entry.replicas.find(renderer);
        if (replica == entry.replicas.end())
            continue;
        if (replica->second.texture != nullptr)
            SDL_DestroyTexture(replica->second.texture);
        retainedBytes_ -= replica->second.bytes;
        entry.bytes -= replica->second.bytes;
        entry.replicas.erase(replica);
    }
    std::erase_if(pendingReplicas_, [&](const ReplicaRequest &request) {
        return request.renderer == renderer;
    });
    if (ownerRenderer_ == renderer)
        ownerRenderer_ = nullptr;
}

void ThumbnailCache::end_frame() noexcept
{
    if (!owner_thread_ok())
        return;
    std::lock_guard lock(mutex_);
    framePins_.clear();
    for (auto &[key, entry] : entries_)
    {
        (void)key;
        entry.demandedPreviousFrame = entry.demandedThisFrame;
        entry.demandedThisFrame = false;
    }
}

ThumbnailCacheStats ThumbnailCache::stats() const noexcept
{
    std::lock_guard lock(mutex_);
    const auto stale = static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(),
        [](const auto &entry) { return entry.second.stale; }));
    const auto outstanding = pending_.size() + (running_ ? 1U : 0U) + (completed_ ? 1U : 0U);
    std::size_t replicas = 0;
    for (const auto &[key, entry] : entries_)
    {
        (void)key;
        replicas += entry.replicas.size();
    }
    return {entries_.size(), retainedBytes_ + completedBytes_ + uploadingBytes_, outstanding,
            completedBytes_ + uploadingBytes_, framePins_.size(), stale, replicas,
            running_, ownerThreadViolation_};
}
} // namespace platformator_editor
