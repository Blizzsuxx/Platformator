#pragma once

#include "async_requests.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace platformator_editor
{
struct FileDialogFilter
{
    std::string description;
    std::string pattern;
};

// The application owns this service. Native callbacks retain only a token and
// copied SDL argument storage; they never retain document, panel, or Runtime
// state. Results are consumed by the caller on its UI thread.
class FileDialogService
{
  public:
    explicit FileDialogService(std::uint64_t sessionGeneration);
    ~FileDialogService();
    FileDialogService(const FileDialogService &) = delete;
    FileDialogService &operator=(const FileDialogService &) = delete;

    AsyncRequestIdentity open_scene(SDL_Window *window, std::uint64_t documentGeneration,
                                    const std::filesystem::path &defaultLocation = {});
    AsyncRequestIdentity open_asset(SDL_Window *window, std::uint64_t documentGeneration,
                                    const std::filesystem::path &assetRoot,
                                    std::string_view expectedAssetType);
    AsyncRequestIdentity save_scene(SDL_Window *window, std::uint64_t documentGeneration,
                                    const std::filesystem::path &defaultLocation = {});
    AsyncRequestIdentity select_folder(SDL_Window *window, std::uint64_t documentGeneration,
                                       const std::filesystem::path &defaultLocation = {});
    AsyncRequestIdentity submit_manual_path(AsyncRequestKind kind, std::uint64_t documentGeneration,
                                            const std::filesystem::path &selectedPath);

    std::vector<AsyncRequestResult> take_ready(std::uint64_t currentDocumentGeneration);
    void shutdown() noexcept;

  private:
    AsyncRequestQueue requests_;
};
} // namespace platformator_editor
