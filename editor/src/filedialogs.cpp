#include "filedialogs.h"

#include <imgui.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace platformator_editor
{
struct AsyncRequestSharedState
{
    explicit AsyncRequestSharedState(std::uint64_t generation) : sessionGeneration(generation) {}

    std::mutex mutex;
    const std::uint64_t sessionGeneration;
    std::uint64_t nextRequestId = 1;
    bool alive = true;
    std::array<std::uint64_t, 4> latestByKind{};
    std::vector<AsyncRequestResult> ready;
};

struct AsyncRequestTicket
{
    std::weak_ptr<AsyncRequestSharedState> state;
    AsyncRequestIdentity identity;
    std::atomic<bool> completed{false};
};

namespace
{
std::size_t kind_index(AsyncRequestKind kind)
{
    const auto index = static_cast<std::size_t>(kind);
    if (index >= 4)
        throw std::invalid_argument("invalid asynchronous request kind");
    return index;
}

void publish(const std::shared_ptr<AsyncRequestTicket> &ticket, AsyncRequestOutcome outcome,
             std::filesystem::path selectedPath, std::string error)
{
    if (!ticket)
        return;
    bool expected = false;
    if (!ticket->completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    const auto shared = ticket->state.lock();
    if (!shared)
        return;

    std::lock_guard lock(shared->mutex);
    if (!shared->alive || ticket->identity.sessionGeneration != shared->sessionGeneration)
        return;
    shared->ready.push_back(AsyncRequestResult{ticket->identity, outcome,
                                               std::move(selectedPath), std::move(error)});
}

struct DialogCallbackContext
{
    AsyncRequestToken token;
    std::vector<std::string> filterNames;
    std::vector<std::string> filterPatterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string defaultLocation;

    DialogCallbackContext(AsyncRequestToken requestToken, std::vector<FileDialogFilter> requestedFilters,
                          const std::filesystem::path &location)
        : token(std::move(requestToken))
    {
        if (!location.empty())
            defaultLocation = location.string();
        filterNames.reserve(requestedFilters.size());
        filterPatterns.reserve(requestedFilters.size());
        filters.reserve(requestedFilters.size());
        for (auto &filter : requestedFilters)
        {
            filterNames.push_back(std::move(filter.description));
            filterPatterns.push_back(std::move(filter.pattern));
        }
        for (std::size_t i = 0; i < filterNames.size(); ++i)
            filters.push_back(SDL_DialogFileFilter{filterNames[i].c_str(), filterPatterns[i].c_str()});
    }
};

void SDLCALL dialog_completed(void *userdata, const char *const *filelist, int) noexcept
{
    std::unique_ptr<DialogCallbackContext> context(static_cast<DialogCallbackContext *>(userdata));
    if (context == nullptr)
        return;

    try
    {
        if (filelist == nullptr)
        {
            const char *nativeError = SDL_GetError();
            context->token.complete_error(nativeError == nullptr || *nativeError == '\0'
                                              ? "native file dialog provider failed"
                                              : nativeError);
            return;
        }
        if (filelist[0] == nullptr)
        {
            context->token.complete_cancelled();
            return;
        }
        context->token.complete_selected(filelist[0]);
    }
    catch (...)
    {
        // Never allow an allocation or path conversion failure to unwind
        // through SDL's callback trampoline. The UI can issue another request.
    }
}

AsyncRequestIdentity show_dialog(AsyncRequestQueue &queue, AsyncRequestKind kind,
                                 SDL_Window *window, std::uint64_t documentGeneration,
                                 const std::filesystem::path &defaultLocation,
                                 std::string_view expectedAssetType = {})
{
    auto token = queue.begin(kind, documentGeneration);
    const auto identity = token.identity();
    auto filters = make_file_dialog_filters(kind, expectedAssetType);
    if (std::any_of(filters.begin(), filters.end(), [](const FileDialogFilter &filter) {
            return !valid_file_dialog_filter_pattern(filter.pattern);
        }))
    {
        token.complete_error("The editor generated an invalid native file-dialog filter.");
        return identity;
    }
    auto context = std::make_unique<DialogCallbackContext>(std::move(token), std::move(filters), defaultLocation);
    auto *callbackContext = context.release();
    const char *location = callbackContext->defaultLocation.empty() ? nullptr
                                                                    : callbackContext->defaultLocation.c_str();
    const auto filterCount = callbackContext->filters.size();
    const auto *filterData = filterCount == 0 ? nullptr : callbackContext->filters.data();
    switch (kind)
    {
    case AsyncRequestKind::OpenScene:
    case AsyncRequestKind::OpenAsset:
        SDL_ShowOpenFileDialog(dialog_completed, callbackContext, window, filterData,
                               static_cast<int>(filterCount), location, false);
        break;
    case AsyncRequestKind::SaveScene:
        SDL_ShowSaveFileDialog(dialog_completed, callbackContext, window, filterData,
                               static_cast<int>(filterCount), location);
        break;
    case AsyncRequestKind::SelectFolder:
        SDL_ShowOpenFolderDialog(dialog_completed, callbackContext, window, location, false);
        break;
    }
    return identity;
}
} // namespace

std::vector<FileDialogFilter> make_file_dialog_filters(AsyncRequestKind kind,
                                                       std::string_view expectedAssetType)
{
    switch (kind)
    {
    case AsyncRequestKind::OpenScene:
    case AsyncRequestKind::SaveScene:
        return {{"Platformator scenes", "scene"}};
    case AsyncRequestKind::OpenAsset:
        if (expectedAssetType == "Texture")
            return {{"Textures", "png;jpg;jpeg;bmp;gif;webp;tga;qoi;tif;tiff"}};
        if (expectedAssetType == "Audio")
            return {{"Audio", "wav;ogg;mp3;flac;opus"}};
        if (expectedAssetType == "AnimationClip")
            return {{"Animation sets", "animset"}};
        return {{"Assets", "*"}};
    case AsyncRequestKind::SelectFolder:
        return {};
    }
    return {};
}

bool valid_file_dialog_filter_pattern(std::string_view pattern) noexcept
{
    if (pattern == "*")
        return true;
    if (pattern.empty())
        return false;

    bool previousWasSeparator = true;
    for (const unsigned char character : pattern)
    {
        if (character == ';')
        {
            if (previousWasSeparator)
                return false;
            previousWasSeparator = true;
            continue;
        }

        const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                                  (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9');
        if (!alphanumeric && character != '-' && character != '_' && character != '.')
            return false;
        previousWasSeparator = false;
    }
    return !previousWasSeparator;
}

FileDialogPathAction draw_file_dialog_path_entry(bool &open, std::string_view providerError,
                                                char *path, std::size_t pathCapacity)
{
    if (path == nullptr || pathCapacity == 0)
        return FileDialogPathAction::None;

    constexpr const char *popupName = "File dialog path entry";
    if (open)
        ImGui::OpenPopup(popupName);

    FileDialogPathAction action = FileDialogPathAction::None;
    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto cancel = [&] {
            ImGui::CloseCurrentPopup();
            open = false;
            path[0] = '\0';
            return FileDialogPathAction::Cancel;
        };
        ImGui::TextWrapped("The native file dialog failed: %.*s",
                           static_cast<int>(providerError.size()), providerError.data());
        ImGui::TextUnformatted("Enter a file or folder path to continue.");
        ImGui::InputText("Path", path, pathCapacity);

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            action = cancel();
        else if (path[0] != '\0' && ImGui::Button("Use path"))
        {
            ImGui::CloseCurrentPopup();
            open = false;
            action = FileDialogPathAction::Submit;
        }
        else
        {
            ImGui::SameLine();
            if (ImGui::Button("Cancel path entry"))
                action = cancel();
        }
        ImGui::EndPopup();
    }
    return action;
}

AsyncRequestToken::AsyncRequestToken(std::shared_ptr<AsyncRequestTicket> ticket)
    : ticket_(std::move(ticket))
{
}

AsyncRequestToken::operator bool() const noexcept
{
    return ticket_ != nullptr;
}

const AsyncRequestIdentity &AsyncRequestToken::identity() const noexcept
{
    static const AsyncRequestIdentity emptyIdentity{};
    return ticket_ ? ticket_->identity : emptyIdentity;
}

void AsyncRequestToken::complete_selected(std::string_view path) const
{
    publish(ticket_, AsyncRequestOutcome::Selected, std::filesystem::path(path), {});
}

void AsyncRequestToken::complete_cancelled() const
{
    publish(ticket_, AsyncRequestOutcome::Cancelled, {}, {});
}

void AsyncRequestToken::complete_error(std::string_view message) const
{
    publish(ticket_, AsyncRequestOutcome::Failed, {}, std::string(message));
}

AsyncRequestQueue::AsyncRequestQueue(std::uint64_t sessionGeneration)
    : state_(std::make_shared<AsyncRequestSharedState>(sessionGeneration))
{
}

AsyncRequestQueue::~AsyncRequestQueue()
{
    shutdown();
}

AsyncRequestToken AsyncRequestQueue::begin(AsyncRequestKind kind, std::uint64_t documentGeneration)
{
    const auto index = kind_index(kind);
    std::lock_guard lock(state_->mutex);
    if (!state_->alive)
        return {};
    if (state_->nextRequestId == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("asynchronous request ID space exhausted");

    auto ticket = std::make_shared<AsyncRequestTicket>();
    ticket->state = state_;
    ticket->identity = AsyncRequestIdentity{state_->nextRequestId++, state_->sessionGeneration,
                                            documentGeneration, kind};
    state_->latestByKind[index] = ticket->identity.requestId;
    return AsyncRequestToken(std::move(ticket));
}

std::vector<AsyncRequestResult> AsyncRequestQueue::take_ready(std::uint64_t currentDocumentGeneration)
{
    std::vector<AsyncRequestResult> results;
    std::lock_guard lock(state_->mutex);
    if (!state_->alive)
        return results;

    results.reserve(state_->ready.size());
    for (auto &result : state_->ready)
    {
        const auto currentRequest = state_->latestByKind[kind_index(result.identity.kind)];
        if (result.identity.sessionGeneration == state_->sessionGeneration &&
            result.identity.documentGeneration == currentDocumentGeneration &&
            result.identity.requestId == currentRequest)
            results.push_back(std::move(result));
    }
    state_->ready.clear();
    return results;
}

void AsyncRequestQueue::shutdown() noexcept
{
    if (!state_)
        return;
    std::lock_guard lock(state_->mutex);
    state_->alive = false;
    state_->ready.clear();
    state_->latestByKind.fill(0);
}

FileDialogService::FileDialogService(std::uint64_t sessionGeneration)
    : requests_(sessionGeneration)
{
}

FileDialogService::~FileDialogService()
{
    shutdown();
}

AsyncRequestIdentity FileDialogService::open_scene(SDL_Window *window, std::uint64_t documentGeneration,
                                                   const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::OpenScene, window, documentGeneration,
                       defaultLocation);
}

AsyncRequestIdentity FileDialogService::open_asset(SDL_Window *window, std::uint64_t documentGeneration,
                                                    const std::filesystem::path &assetRoot,
                                                    std::string_view expectedAssetType)
{
    return show_dialog(requests_, AsyncRequestKind::OpenAsset, window, documentGeneration,
                       assetRoot, expectedAssetType);
}

AsyncRequestIdentity FileDialogService::save_scene(SDL_Window *window, std::uint64_t documentGeneration,
                                                   const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SaveScene, window, documentGeneration,
                       defaultLocation);
}

AsyncRequestIdentity FileDialogService::select_folder(SDL_Window *window, std::uint64_t documentGeneration,
                                                      const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SelectFolder, window, documentGeneration,
                       defaultLocation, {});
}

AsyncRequestIdentity FileDialogService::select_project_folder(SDL_Window *window,
                                                               std::uint64_t contextGeneration,
                                                               const std::filesystem::path &defaultLocation)
{
    return select_folder(window, contextGeneration, defaultLocation);
}

AsyncRequestIdentity FileDialogService::submit_manual_path(AsyncRequestKind kind,
                                                           std::uint64_t documentGeneration,
                                                           const std::filesystem::path &selectedPath)
{
    auto token = requests_.begin(kind, documentGeneration);
    const auto identity = token.identity();
    token.complete_selected(selectedPath.string());
    return identity;
}

std::vector<AsyncRequestResult> FileDialogService::take_ready(std::uint64_t currentDocumentGeneration)
{
    return requests_.take_ready(currentDocumentGeneration);
}

void FileDialogService::shutdown() noexcept
{
    requests_.shutdown();
}
} // namespace platformator_editor
