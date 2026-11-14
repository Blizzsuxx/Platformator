#include "filedialogs.h"

#include <imgui.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <limits>
#include <map>
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
    std::array<std::uint64_t, 6> latestByKind{};
    struct ScopedLatest
    {
        DocumentIdentity identity;
        std::uint64_t requestId = 0;
    };
    std::map<std::pair<std::uint8_t, std::uint64_t>, ScopedLatest> latestScoped;
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
    if (index >= 6)
        throw std::invalid_argument("invalid asynchronous request kind");
    return index;
}

std::string path_to_utf8(const std::filesystem::path &path)
{
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

std::filesystem::path path_from_utf8(std::string_view value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
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
            defaultLocation = path_to_utf8(location);
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
    case AsyncRequestKind::OpenExecutable:
        SDL_ShowOpenFileDialog(dialog_completed, callbackContext, window, filterData,
                               static_cast<int>(filterCount), location, false);
        break;
    case AsyncRequestKind::SaveScene:
    case AsyncRequestKind::SaveAnimation:
        SDL_ShowSaveFileDialog(dialog_completed, callbackContext, window, filterData,
                               static_cast<int>(filterCount), location);
        break;
    case AsyncRequestKind::SelectFolder:
        SDL_ShowOpenFolderDialog(dialog_completed, callbackContext, window, location, false);
        break;
    }
    return identity;
}

AsyncRequestIdentity show_dialog(AsyncRequestQueue &queue, AsyncRequestKind kind,
                                 SDL_Window *window, DocumentIdentity documentIdentity,
                                 const std::filesystem::path &defaultLocation,
                                 std::string_view expectedAssetType = {})
{
    auto token = queue.begin(kind, documentIdentity);
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
    case AsyncRequestKind::OpenExecutable:
        SDL_ShowOpenFileDialog(dialog_completed, callbackContext, window, filterData,
                               static_cast<int>(filterCount), location, false);
        break;
    case AsyncRequestKind::SaveScene:
    case AsyncRequestKind::SaveAnimation:
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
    case AsyncRequestKind::SaveAnimation:
        return {{"Animation sets", "animset"}};
    case AsyncRequestKind::OpenAsset:
        if (expectedAssetType == "Template")
            return {{"Platformator templates", "pttemplate"}};
        if (expectedAssetType == "Texture")
            return {{"Textures", "png;jpg;jpeg;bmp;gif;webp;tga;qoi;tif;tiff"}};
        if (expectedAssetType == "Audio")
            return {{"Audio", "wav;ogg;mp3;flac;opus"}};
        if (expectedAssetType == "AnimationClip")
            return {{"Animation sets", "animset"}};
        return {{"Assets", "*"}};
    case AsyncRequestKind::OpenExecutable:
#if defined(_WIN32)
        return {{"Windows executables", "exe"}};
#else
        return {{"Executable files", "*"}};
#endif
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
            if (path[0] != '\0')
                ImGui::SameLine();
            if (ImGui::Button("Cancel"))
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
    publish(ticket_, AsyncRequestOutcome::Selected, path_from_utf8(path), {});
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

AsyncRequestToken AsyncRequestQueue::begin(AsyncRequestKind kind, DocumentIdentity identity)
{
    (void)kind_index(kind);
    if (!identity.projectGeneration || !identity.tabId || !identity.documentGeneration)
        throw std::invalid_argument("scoped asynchronous requests require project, tab, and document identities");
    std::lock_guard lock(state_->mutex);
    if (!state_->alive)
        return {};
    if (state_->nextRequestId == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("asynchronous request ID space exhausted");

    auto ticket = std::make_shared<AsyncRequestTicket>();
    ticket->state = state_;
    ticket->identity = AsyncRequestIdentity{state_->nextRequestId++, state_->sessionGeneration,
                                            identity.documentGeneration.value, kind, identity};
    const auto key = std::pair{static_cast<std::uint8_t>(kind), identity.tabId.value};
    state_->latestScoped[key] = {identity, ticket->identity.requestId};
    return AsyncRequestToken(std::move(ticket));
}

std::vector<AsyncRequestResult> AsyncRequestQueue::take_ready(std::uint64_t currentDocumentGeneration)
{
    std::vector<AsyncRequestResult> results;
    std::lock_guard lock(state_->mutex);
    if (!state_->alive)
        return results;

    results.reserve(state_->ready.size());
    auto output = state_->ready.begin();
    for (auto input = state_->ready.begin(); input != state_->ready.end(); ++input)
    {
        const bool scoped = static_cast<bool>(input->identity.documentIdentity.projectGeneration) ||
                            static_cast<bool>(input->identity.documentIdentity.tabId);
        if (!scoped && input->identity.sessionGeneration == state_->sessionGeneration &&
            input->identity.documentGeneration == currentDocumentGeneration &&
            input->identity.requestId == state_->latestByKind[kind_index(input->identity.kind)])
            results.push_back(std::move(*input));
        else if (scoped)
        {
            if (output != input)
                *output = std::move(*input);
            ++output;
        }
    }
    state_->ready.erase(output, state_->ready.end());
    return results;
}

std::vector<AsyncRequestResult> AsyncRequestQueue::take_ready(const DocumentIdentity &currentIdentity)
{
    return take_ready([&](const DocumentIdentity &identity) { return identity == currentIdentity; });
}

std::vector<AsyncRequestResult> AsyncRequestQueue::take_ready(
    const std::function<bool(const DocumentIdentity &)> &identityIsCurrent)
{
    std::vector<AsyncRequestResult> results;
    std::lock_guard lock(state_->mutex);
    if (!state_->alive)
        return results;

    results.reserve(state_->ready.size());
    auto output = state_->ready.begin();
    for (auto input = state_->ready.begin(); input != state_->ready.end(); ++input)
    {
        const auto &identity = input->identity.documentIdentity;
        const bool scoped = static_cast<bool>(identity.projectGeneration) || static_cast<bool>(identity.tabId);
        if (!scoped)
        {
            if (output != input)
                *output = std::move(*input);
            ++output;
            continue;
        }
        const auto key = std::pair{static_cast<std::uint8_t>(input->identity.kind), identity.tabId.value};
        const auto latest = state_->latestScoped.find(key);
        const bool latestRequest = input->identity.sessionGeneration == state_->sessionGeneration &&
                                  latest != state_->latestScoped.end() &&
                                  latest->second.identity == identity &&
                                  input->identity.requestId == latest->second.requestId;
        if (!latestRequest)
            continue;
        if (identityIsCurrent && identityIsCurrent(identity))
            results.push_back(std::move(*input));
        else
        {
            if (output != input)
                *output = std::move(*input);
            ++output;
        }
    }
    state_->ready.erase(output, state_->ready.end());
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
    state_->latestScoped.clear();
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

AsyncRequestIdentity FileDialogService::open_scene(SDL_Window *window, DocumentIdentity identity,
                                                    const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::OpenScene, window, identity, defaultLocation);
}

AsyncRequestIdentity FileDialogService::open_asset(SDL_Window *window, std::uint64_t documentGeneration,
                                                    const std::filesystem::path &assetRoot,
                                                    std::string_view expectedAssetType)
{
    return show_dialog(requests_, AsyncRequestKind::OpenAsset, window, documentGeneration,
                       assetRoot, expectedAssetType);
}

AsyncRequestIdentity FileDialogService::open_asset(SDL_Window *window, DocumentIdentity identity,
                                                    const std::filesystem::path &assetRoot,
                                                    std::string_view expectedAssetType)
{
    return show_dialog(requests_, AsyncRequestKind::OpenAsset, window, identity,
                       assetRoot, expectedAssetType);
}

AsyncRequestIdentity FileDialogService::open_executable(
    SDL_Window *window, DocumentIdentity identity, const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::OpenExecutable, window, identity, defaultLocation);
}

AsyncRequestIdentity FileDialogService::save_scene(SDL_Window *window, std::uint64_t documentGeneration,
                                                   const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SaveScene, window, documentGeneration,
                       defaultLocation);
}

AsyncRequestIdentity FileDialogService::save_scene(SDL_Window *window, DocumentIdentity identity,
                                                   const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SaveScene, window, identity, defaultLocation);
}

AsyncRequestIdentity FileDialogService::save_animation(
    SDL_Window *window, DocumentIdentity identity,
    const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SaveAnimation, window, identity, defaultLocation);
}

AsyncRequestIdentity FileDialogService::select_folder(SDL_Window *window, std::uint64_t documentGeneration,
                                                      const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SelectFolder, window, documentGeneration,
                       defaultLocation, {});
}

AsyncRequestIdentity FileDialogService::select_folder(SDL_Window *window, DocumentIdentity identity,
                                                       const std::filesystem::path &defaultLocation)
{
    return show_dialog(requests_, AsyncRequestKind::SelectFolder, window, identity, defaultLocation, {});
}

AsyncRequestIdentity FileDialogService::select_project_folder(SDL_Window *window,
                                                               std::uint64_t contextGeneration,
                                                               const std::filesystem::path &defaultLocation)
{
    return select_folder(window, contextGeneration, defaultLocation);
}

AsyncRequestIdentity FileDialogService::select_project_folder(SDL_Window *window,
                                                               DocumentIdentity identity,
                                                               const std::filesystem::path &defaultLocation)
{
    return select_folder(window, identity, defaultLocation);
}

AsyncRequestIdentity FileDialogService::submit_manual_path(AsyncRequestKind kind,
                                                           std::uint64_t documentGeneration,
                                                           const std::filesystem::path &selectedPath)
{
    auto token = requests_.begin(kind, documentGeneration);
    const auto identity = token.identity();
    token.complete_selected(path_to_utf8(selectedPath));
    return identity;
}

AsyncRequestIdentity FileDialogService::submit_manual_path(AsyncRequestKind kind,
                                                           DocumentIdentity identity,
                                                           const std::filesystem::path &selectedPath)
{
    auto token = requests_.begin(kind, identity);
    const auto requestIdentity = token.identity();
    token.complete_selected(path_to_utf8(selectedPath));
    return requestIdentity;
}

std::vector<AsyncRequestResult> FileDialogService::take_ready(std::uint64_t currentDocumentGeneration)
{
    return requests_.take_ready(currentDocumentGeneration);
}

std::vector<AsyncRequestResult> FileDialogService::take_ready(const DocumentIdentity &currentIdentity)
{
    return requests_.take_ready(currentIdentity);
}

std::vector<AsyncRequestResult> FileDialogService::take_ready(
    const std::function<bool(const DocumentIdentity &)> &identityIsCurrent)
{
    return requests_.take_ready(identityIsCurrent);
}

void FileDialogService::shutdown() noexcept
{
    requests_.shutdown();
}
} // namespace platformator_editor
