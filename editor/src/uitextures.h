#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platformator_editor
{
// Produces renderer-local copies of the main Scene preview for detached hosts.
// The transfer target, CPU staging buffer, transient SDL readback surface and
// destination replica are kept within one aggregate allocation budget.
class SceneTextureReplicas
{
  public:
    static constexpr std::size_t MaxTransferBytes = 64ULL * 1024ULL * 1024ULL;

    SceneTextureReplicas() = default;
    SceneTextureReplicas(const SceneTextureReplicas &) = delete;
    SceneTextureReplicas &operator=(const SceneTextureReplicas &) = delete;
    ~SceneTextureReplicas();

    SDL_Texture *texture_for(SDL_Renderer *sourceRenderer, SDL_Texture *sourceTexture,
                             std::uint64_t sourceRevision, SDL_Renderer *destinationRenderer,
                             int desiredWidth, int desiredHeight) noexcept;
    void release_renderer(SDL_Renderer *renderer) noexcept;
    void release_all() noexcept;
    std::size_t allocated_bytes() const noexcept;

  private:
    void clear_transfer() noexcept;
    void clear_replica() noexcept;

    SDL_Renderer *sourceRenderer_ = nullptr;
    SDL_Renderer *destinationRenderer_ = nullptr;
    SDL_Texture *transferTarget_ = nullptr;
    SDL_Texture *replica_ = nullptr;
    SDL_Texture *lastSourceTexture_ = nullptr;
    std::uint64_t sourceRevision_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<std::byte> staging_;
};
} // namespace platformator_editor
