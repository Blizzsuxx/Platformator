#pragma once

#include "../accessibility.h"
#include "../playsession.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace platformator_editor
{
enum class GameAspectPreset
{
    FreeAspect,
    Aspect16x9,
    Aspect4x3,
    Native640x360
};

class GamePanel
{
  public:
    static constexpr int FrameWidth = 640;
    static constexpr int FrameHeight = 360;
    static constexpr int FrameStride = FrameWidth * 4;
    static constexpr std::size_t FrameBytes = static_cast<std::size_t>(FrameStride) * FrameHeight;

    GamePanel() = default;
    ~GamePanel();
    GamePanel(const GamePanel &) = delete;
    GamePanel &operator=(const GamePanel &) = delete;

    void set_renderer(SDL_Renderer *renderer) noexcept;
    static std::string asset_change_status_message(bool liveAssetReloadAdvertised);
    void consume_event(PlaySession &session,
                       const platformator::editor_session::Message &message);
    void set_pipeline_status(std::string message, bool restartAvailable = false);
    void set_restart_callback(std::function<void()> callback);
    void set_visible(PlaySession *session, bool visible);
    void draw(PlaySession *session, bool visible);
    std::vector<SemanticNode> semantic_nodes(PlaySession *session,
                                              const std::string &parentId);
    bool dispatch_semantic_action(PlaySession &session,
                                  const SemanticAction &action);
    const std::string &status() const noexcept;
    void release_renderer(SDL_Renderer *renderer) noexcept;
    bool is_input_captured() const noexcept { return capturingInput_; }
    void capture_input() noexcept { capturingInput_ = true; }
    void release_input_capture(PlaySession *session = nullptr);
    void handle_sdl_event(PlaySession *session, const SDL_Event &event);

  private:
    void observe_generation(PlaySession *session);
    bool upload_frame(PlaySession &session, std::uint32_t slot,
                      std::uint64_t sequence);
    SDL_Texture *ensure_frame_texture(SDL_Renderer *renderer) noexcept;
    void draw_runtime_inspection(PlaySession *session);
    static std::string object_semantic_id(const Json &handle);

    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *frameTexture_ = nullptr;
    std::unordered_map<SDL_Renderer *, SDL_Texture *> frameTextures_;
    std::vector<std::byte> framePixels_ = std::vector<std::byte>(FrameBytes);
    bool hasFramePixels_ = false;
    PlayGeneration generation_;
    bool captureVisible_ = false;
    std::string status_ = "No embedded game session is active.";
    std::string pipelineStatus_;
    bool restartAvailable_ = false;
    std::function<void()> restartCallback_;
    std::vector<Json> runtimeObjects_;
    std::optional<Json> selectedHandle_;
    Json selectedSnapshot_ = Json::object();
    std::uint64_t snapshotRevision_ = 0;
    std::optional<std::uint64_t> nextCursor_;
    bool awaitingRuntimePage_ = false;
    std::unordered_map<std::string, Json> semanticHandles_;
    GameAspectPreset aspectPreset_ = GameAspectPreset::Aspect16x9;
    float zoomScale_ = 1.0f;
    bool capturingInput_ = false;
};
} // namespace platformator_editor
