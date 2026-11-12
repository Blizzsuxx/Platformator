#pragma once

#include "../assetdocument.h"
#include "../accessibility.h"
#include "numericdraft.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platformator_editor
{
class AnimationFrameRegionEditor;

AssetEditResult add_animation_frame(AssetDocument &document, Json frame);
AssetEditResult remove_animation_frame(AssetDocument &document, std::size_t index);
AssetEditResult move_animation_frame(AssetDocument &document, std::size_t from,
                                     std::size_t to);
bool animation_texture_is_compatible(const std::filesystem::path &assetRoot,
                                     std::string_view textureReference);
std::size_t animation_frame_at_time(const Json &clip, double seconds) noexcept;

class AnimationPanel
{
  public:
    struct PreparedTextureInfo
    {
        SDL_Texture *texture = nullptr;
        int sourceWidth = 0;
        int sourceHeight = 0;
    };
    using PreparedTexture = std::function<PreparedTextureInfo(const std::filesystem::path &)>;

    AnimationPanel();
    ~AnimationPanel();

    void draw(AssetDocument &document, const std::filesystem::path &assetRoot,
              const PreparedTexture &preparedTexture = {});
    void scrub_to(std::size_t frameIndex) noexcept;
    void scrub_to(const Json &clip, std::size_t frameIndex) noexcept;
    void set_playing(bool playing) noexcept;
    void advance_playback(const Json &clip, double deltaSeconds) noexcept;
    std::optional<std::chrono::steady_clock::time_point> next_playback_deadline(
        const Json &clip, std::chrono::steady_clock::time_point now) const noexcept;
    void cancel_numeric_drafts() noexcept;

    std::size_t selected_frame() const noexcept { return selectedFrame_; }
    bool playing() const noexcept { return playing_; }
    std::uint64_t view_revision() const noexcept { return viewRevision_; }
    std::vector<SemanticNode> semantic_nodes(const AssetDocument &document,
                                             const std::filesystem::path &assetRoot,
                                             std::string_view scope,
                                             const PreparedTexture &preparedTexture = {});
    bool dispatch_semantic_action(AssetDocument &document,
                                  const std::filesystem::path &assetRoot,
                                  const SemanticAction &action,
                                  std::string_view scope,
                                  std::string &error);

  private:
    void sync_texture_draft(const Json &frame);

    std::size_t selectedFrame_ = 0;
    double playbackSeconds_ = 0.0;
    bool playing_ = false;
    std::size_t textureDraftFrame_ = static_cast<std::size_t>(-1);
    std::string textureDraft_;
    std::array<char, 1024> textureDraftBuffer_{};
    std::string feedback_;
    std::unique_ptr<AnimationFrameRegionEditor> sourceRegionEditor_;
    std::size_t semanticSourceFrame_ = static_cast<std::size_t>(-1);
    int semanticSourceWidth_ = 0;
    int semanticSourceHeight_ = 0;
    std::string semanticSourceReference_;
    std::uint64_t viewRevision_ = 1;
    AddressNumericDraft durationDraft_;
};
} // namespace platformator_editor
