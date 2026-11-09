#pragma once

#include "../editcommands.h"
#include "../assetdocument.h"
#include "numericdraft.h"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <utility>
#include <filesystem>

namespace platformator_editor
{
struct SpriteSourceRegion
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct AnimationFrameRegionDestination
{
    AssetDocument &document;
    std::size_t frameIndex = 0;
};

class AnimationFrameRegionEditor
{
  public:
    void cancel_numeric_drafts() noexcept;
    bool draw(AssetDocument &document, std::size_t frameIndex,
              const std::filesystem::path &assetRoot,
              std::string_view textureReference, SDL_Texture *preparedThumbnail,
              int sourceWidth, int sourceHeight);

  private:
    bool dragging_ = false;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    const AssetDocument *document_ = nullptr;
    std::size_t frameIndex_ = 0;
    std::filesystem::path assetRoot_;
    std::string textureReference_;
    std::string feedback_;
    AddressNumericDraft xDraft_;
    AddressNumericDraft yDraft_;
    AddressNumericDraft widthDraft_;
    AddressNumericDraft heightDraft_;
};

bool validate_sprite_source_region(const SpriteSourceRegion &region,
                                   int sourceWidth, int sourceHeight,
                                   std::string &error);

CommandResult commit_sprite_source_region(EditCommandService &commands,
                                          DocumentSession &document,
                                          int objectId, int componentId,
                                          std::string_view textureReference,
                                          const SpriteSourceRegion &region,
                                          int sourceWidth, int sourceHeight);

AssetEditResult commit_animation_frame_source_region(
    AnimationFrameRegionDestination destination,
    std::string_view textureReference,
    const std::filesystem::path &assetRoot,
    const SpriteSourceRegion &region,
    int sourceWidth, int sourceHeight);
} // namespace platformator_editor
