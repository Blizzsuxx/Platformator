#include "spriteeditor.h"

#include "animation.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#include <imgui.h>

#include "../document.h"
#include "../style.h"

namespace platformator_editor
{
namespace
{
std::string frame_region_numeric_address(const AssetDocument &document,
                                         std::size_t frameIndex,
                                         const std::filesystem::path &assetRoot,
                                         std::string_view textureReference,
                                         std::string_view field)
{
    const std::string source = document.source_path()
        ? document.source_path()->generic_string() : std::string("untitled");
    const std::string root = assetRoot.generic_string();
    return std::to_string(document.generation()) + ":" + std::to_string(source.size()) + ":" +
        source + ":" + std::to_string(root.size()) + ":" + root + ":" +
        std::to_string(frameIndex) + ":" + std::to_string(textureReference.size()) + ":" +
        std::string(textureReference) + ":" + std::string(field);
}

const Json *object_at(const Json &root, const ObjectLocation &location)
{
    if (!root.is_array() || location.childIndices.empty())
        return nullptr;
    const Json *object = nullptr;
    const Json *siblings = &root;
    for (std::size_t depth = 0; depth < location.childIndices.size(); ++depth)
    {
        if (depth != 0)
        {
            if (object == nullptr)
                return nullptr;
            const auto children = object->find("children");
            if (children == object->end())
                return nullptr;
            siblings = &*children;
        }
        const auto index = location.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        object = &(*siblings)[index];
    }
    return object;
}
} // namespace

void AnimationFrameRegionEditor::cancel_numeric_drafts() noexcept
{
    widget_.cancel_drag();
    widget_.cancel_numeric_drafts();
}

bool validate_sprite_source_region(const SpriteSourceRegion &region,
                                   int sourceWidth, int sourceHeight,
                                   std::string &error)
{
    error.clear();
    if (sourceWidth <= 0 || sourceHeight <= 0)
    {
        error = "Source image dimensions are unavailable.";
        return false;
    }
    if (!std::isfinite(region.x) || !std::isfinite(region.y) ||
        !std::isfinite(region.width) || !std::isfinite(region.height))
    {
        error = "Source region values must be finite.";
        return false;
    }
    if (region.x < 0.0 || region.y < 0.0 || region.width <= 0.0 || region.height <= 0.0)
    {
        error = "Source region origin must be nonnegative and its size must be positive.";
        return false;
    }
    const double right = region.x + region.width;
    const double bottom = region.y + region.height;
    if (!std::isfinite(right) || !std::isfinite(bottom) ||
        right > static_cast<double>(sourceWidth) || bottom > static_cast<double>(sourceHeight))
    {
        error = "Source region must fit inside the original image dimensions.";
        return false;
    }
    return true;
}

CommandResult commit_sprite_source_region(EditCommandService &commands,
                                          DocumentSession &document,
                                          int objectId, int componentId,
                                          std::string_view textureReference,
                                          const SpriteSourceRegion &region,
                                          int sourceWidth, int sourceHeight)
{
    std::string validationError;
    if (!validate_sprite_source_region(region, sourceWidth, sourceHeight, validationError))
        return {CommandStatus::Rejected, std::move(validationError), {}, {}, std::nullopt};
    const std::filesystem::path relative(textureReference);
    if (textureReference.size() <= 7 || !textureReference.starts_with("assets/") ||
        relative.is_absolute() || std::any_of(relative.begin(), relative.end(), [](const auto &part) {
            return part == "..";
        }))
        return {CommandStatus::Rejected, "Texture path must stay inside the project asset root.",
                {}, {}, std::nullopt};

    const auto componentLocation = document.indexes.componentsById.find(componentId);
    if (componentLocation == document.indexes.componentsById.end() ||
        componentLocation->second.ownerObjectId != objectId)
        return {CommandStatus::StaleAddress, "Selected Sprite component is no longer available.",
                {}, {}, std::nullopt};
    const auto objectLocation = document.indexes.objectsById.find(objectId);
    const Json *object = objectLocation == document.indexes.objectsById.end()
        ? nullptr : object_at(document.root, objectLocation->second);
    if (object == nullptr)
        return {CommandStatus::StaleAddress, "Selected Sprite object is no longer available.",
                {}, {}, std::nullopt};
    const auto components = object->find("components");
    if (components == object->end() || !components->is_array() ||
        componentLocation->second.componentIndex >= components->size())
        return {CommandStatus::StaleAddress, "Selected Sprite component is no longer available.",
                {}, {}, std::nullopt};
    const auto &component = (*components)[componentLocation->second.componentIndex];
    const auto type = component.is_object() ? component.find("type") : Json::const_iterator{};
    if (!component.is_object() || type == component.end() || !type->is_number_integer() ||
        type->get<int>() != 6)
        return {CommandStatus::Rejected, "Source regions can only be assigned to Sprite components.",
                {}, {}, std::nullopt};

    Json sourceRect = Json::object();
    const auto existingRect = component.find("sourceRect");
    if (existingRect != component.end() && existingRect->is_object())
        sourceRect = *existingRect;
    sourceRect["x"] = region.x;
    sourceRect["y"] = region.y;
    sourceRect["w"] = region.width;
    sourceRect["h"] = region.height;

    const std::vector<BatchPropertyEdit> edits{
        {address_for_document(document, objectId, componentId, std::nullopt,
                              {std::string("textureFilePath")}),
         PropertyValue::of(std::string(textureReference))},
        {address_for_document(document, objectId, componentId, std::nullopt,
                              {std::string("sourceRectEnabled")}), PropertyValue::of(true)},
        {address_for_document(document, objectId, componentId, std::nullopt,
                              {std::string("sourceRect")}), PropertyValue::of(std::move(sourceRect))}};
    return commands.apply_batch(document, edits);
}

AssetEditResult commit_animation_frame_source_region(
    AnimationFrameRegionDestination destination,
    std::string_view textureReference,
    const std::filesystem::path &assetRoot,
    const SpriteSourceRegion &region,
    int sourceWidth, int sourceHeight)
{
    std::string validationError;
    if (!validate_sprite_source_region(region, sourceWidth, sourceHeight, validationError))
        return {false, std::move(validationError)};
    if (!animation_texture_is_compatible(assetRoot, textureReference))
        return {false, "Animation frames require an existing image inside the configured asset folder."};

    const Json &root = destination.document.root();
    const auto frames = root.find("frames");
    if (frames == root.end() || !frames->is_array() || destination.frameIndex >= frames->size())
        return {false, "Selected animation frame is no longer available."};

    Json nextFrames = *frames;
    Json &frame = nextFrames[destination.frameIndex];
    if (!frame.is_object())
        return {false, "Selected animation frame is not an object."};

    Json sourceRect = Json::object();
    const auto existingRect = frame.find("sourceRect");
    if (existingRect != frame.end() && existingRect->is_object())
        sourceRect = *existingRect;
    sourceRect["x"] = region.x;
    sourceRect["y"] = region.y;
    sourceRect["w"] = region.width;
    sourceRect["h"] = region.height;
    frame["textureWrapperFilePath"] = std::string(textureReference);
    frame["hasSourceRect"] = true;
    frame["sourceRect"] = std::move(sourceRect);
    return destination.document.set("/frames", std::move(nextFrames));
}

void SpriteSourceRegionWidget::cancel_numeric_drafts() noexcept
{
    xDraft_.cancel();
    yDraft_.cancel();
    widthDraft_.cancel();
    heightDraft_.cancel();
    activeNumericScope_.clear();
}

bool SpriteSourceRegionWidget::draw(
    const char *canvasButtonId,
    SDL_Texture *preparedThumbnail,
    int sourceWidth, int sourceHeight,
    const SpriteSourceRegion &currentRegion,
    const std::function<std::string(std::string_view)> &addressForField,
    const std::function<CommitOutcome(const SpriteSourceRegion &)> &onCommit,
    bool allowSameLineNumericFields, SourceRegionSection section,
    const std::function<bool(std::string_view)> &isFieldOverridden,
    const std::function<void(std::string_view)> &drawFieldOverrideContext)
{
    const std::string dragScope = addressForField ? addressForField("canvas") : std::string(canvasButtonId);
    if (dragging_ && activeDragScope_ != dragScope)
        cancel_drag();
    if (!activeNumericScope_.empty() && activeNumericScope_ != dragScope)
        cancel_numeric_drafts();
    activeNumericScope_ = dragScope;

    if (preparedThumbnail == nullptr || sourceWidth <= 0 || sourceHeight <= 0)
    {
        cancel_drag();
        cancel_numeric_drafts();
        ImGui::TextDisabled("Preparing the source image and its original dimensions...");
        return false;
    }

    float thumbnailWidth = 0.0f;
    float thumbnailHeight = 0.0f;
    if (!SDL_GetTextureSize(preparedThumbnail, &thumbnailWidth, &thumbnailHeight) ||
        thumbnailWidth <= 0.0f || thumbnailHeight <= 0.0f)
    {
        ImGui::TextDisabled("Source thumbnail dimensions are unavailable.");
        cancel_drag();
        return false;
    }

    SpriteSourceRegion region = currentRegion;
    if (section != SourceRegionSection::Fields)
    {
        const float maximumWidth = std::min(std::max(1.0f, ImGui::GetContentRegionAvail().x), 360.0f);
        const float maximumHeight = ImGui::GetTextLineHeight() * 12.0f;
        const float scale = std::min(maximumWidth / thumbnailWidth, maximumHeight / thumbnailHeight);
        const ImVec2 displaySize{std::max(1.0f, thumbnailWidth * scale),
                                 std::max(1.0f, thumbnailHeight * scale)};
        ImGui::InvisibleButton(canvasButtonId, displaySize);
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        lastCanvasMin_ = minimum;
        lastCanvasMax_ = maximum;
        const bool hovered = ImGui::IsItemHovered();
        const bool canvasOverridden = isFieldOverridden && isFieldOverridden("canvas");
        if (canvasOverridden && hovered)
            ImGui::SetItemTooltip("This field overrides its linked template value.");
        else if (hovered)
            ImGui::SetTooltip("%d × %d pixels", sourceWidth, sourceHeight);
        if (drawFieldOverrideContext)
            drawFieldOverrideContext("canvas");
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(preparedThumbnail));
        drawList->AddImage(textureId, minimum, maximum);
        drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border));

        const auto pointToSource = [&](ImVec2 point) {
            const float localX = std::clamp(point.x - minimum.x, 0.0f, displaySize.x);
            const float localY = std::clamp(point.y - minimum.y, 0.0f, displaySize.y);
            return std::pair<double, double>{
                std::floor(static_cast<double>(localX / displaySize.x) * sourceWidth),
                std::floor(static_cast<double>(localY / displaySize.y) * sourceHeight)};
        };
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto [x, y] = pointToSource(ImGui::GetIO().MousePos);
            dragStartX_ = static_cast<float>(x);
            dragStartY_ = static_cast<float>(y);
            activeDragScope_ = dragScope;
            dragging_ = true;
        }

        if (dragging_)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                cancel_drag();
                feedback_ = "Source region drag cancelled.";
            }
            else
            {
                const auto [x, y] = pointToSource(ImGui::GetIO().MousePos);
                const SpriteSourceRegion dragged{std::min<double>(dragStartX_, x),
                    std::min<double>(dragStartY_, y), std::abs(x - dragStartX_),
                    std::abs(y - dragStartY_)};
                if (dragged.width > 0.0 && dragged.height > 0.0)
                {
                    region = dragged;
                    const ImVec2 rectangleMinimum{
                        minimum.x + static_cast<float>(dragged.x) * displaySize.x / sourceWidth,
                        minimum.y + static_cast<float>(dragged.y) * displaySize.y / sourceHeight};
                    const ImVec2 rectangleMaximum{
                        rectangleMinimum.x + static_cast<float>(dragged.width) * displaySize.x / sourceWidth,
                        rectangleMinimum.y + static_cast<float>(dragged.height) * displaySize.y / sourceHeight};
                    drawList->AddRect(rectangleMinimum, rectangleMaximum,
                                      ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 2.0f);
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    dragging_ = false;
                    const auto result = onCommit(dragged);
                    feedback_ = result.feedback;
                    return result.changed;
                }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    dragging_ = false;
            }
        }

        if (!dragging_)
        {
            const ImVec2 rectangleMinimum{
                minimum.x + static_cast<float>(region.x) * displaySize.x / sourceWidth,
                minimum.y + static_cast<float>(region.y) * displaySize.y / sourceHeight};
            const ImVec2 rectangleMaximum{
                rectangleMinimum.x + static_cast<float>(region.width) * displaySize.x / sourceWidth,
                rectangleMinimum.y + static_cast<float>(region.height) * displaySize.y / sourceHeight};
            drawList->AddRect(rectangleMinimum, rectangleMaximum,
                              ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 2.0f);
        }

    }
    if (section == SourceRegionSection::Preview)
        return false;

    const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
    // Two paired rows match vector fields in the Inspector, even in narrow docks.
    const float fieldWidth = std::min(180.0f * uiScale,
        std::max(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f));
    (void)allowSameLineNumericFields;
    const auto submit_field = [&](AddressNumericDraft &draft, const char *label,
                                  std::string_view field, double committed,
                                  double step, auto update) {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        const bool overridden = isFieldOverridden && isFieldOverridden(field);
        if (overridden)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.27f, 0.24f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.32f, 0.28f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.37f, 0.32f, 0.20f, 1.0f));
        }
        if (field == "x" || field == "y")
            ImGui::TextColored(field == "x" ? kAxisXColor
                                           : kAxisYColor, "%s", label);
        else
            ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(1.0f, fieldWidth - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemSpacing.x));
        const std::string hiddenLabel = std::string("##") + label;
        const auto outcome = draft.draw(hiddenLabel.c_str(), addressForField(field), committed, step, false);
        if (overridden)
        {
            ImGui::SetItemTooltip("This field overrides its linked template value.");
            if (drawFieldOverrideContext)
                drawFieldOverrideContext(field);
            ImGui::PopStyleColor(3);
        }
        else if (drawFieldOverrideContext)
            drawFieldOverrideContext(field);
        ImGui::EndGroup();
        if (outcome.kind == NumericDraftOutcomeKind::Invalid)
        {
            feedback_ = "Enter a complete finite source-region value.";
            return false;
        }
        if (outcome.kind != NumericDraftOutcomeKind::Commit)
            return false;
        SpriteSourceRegion candidate = region;
        update(candidate, outcome.value);
        std::string error;
        if (!validate_sprite_source_region(candidate, sourceWidth, sourceHeight, error))
        {
            feedback_ = std::move(error);
            return false;
        }
        const auto result = onCommit(candidate);
        feedback_ = result.feedback;
        if (result.changed)
        {
            cancel_numeric_drafts();
            return true;
        }
        return false;
    };
    if (submit_field(xDraft_, "X", "x", region.x, 1.0,
                     [](SpriteSourceRegion &value, double next) { value.x = next; }))
        return true;
    ImGui::SameLine();
    if (submit_field(yDraft_, "Y", "y", region.y, 1.0,
                     [](SpriteSourceRegion &value, double next) { value.y = next; }))
        return true;

    if (submit_field(widthDraft_, "Width", "width", region.width, 1.0,
                     [](SpriteSourceRegion &value, double next) { value.width = next; }))
        return true;
    ImGui::SameLine();
    if (submit_field(heightDraft_, "Height", "height", region.height, 1.0,
                     [](SpriteSourceRegion &value, double next) { value.height = next; }))
        return true;
    if (!feedback_.empty())
        ImGui::TextWrapped("%s", feedback_.c_str());
    return false;
}

bool AnimationFrameRegionEditor::draw(AssetDocument &document, std::size_t frameIndex,
                                      const std::filesystem::path &assetRoot,
                                      std::string_view textureReference,
                                      SDL_Texture *preparedThumbnail,
                                      int sourceWidth, int sourceHeight,
                                      SourceRegionSection section)
{
    if (document_ != &document || frameIndex_ != frameIndex ||
        assetRoot_ != assetRoot || textureReference_ != textureReference)
        widget_.cancel_drag();
    document_ = &document;
    frameIndex_ = frameIndex;
    assetRoot_ = assetRoot;
    textureReference_ = std::string(textureReference);

    const Json *frames = document.root().contains("frames") &&
                         document.root()["frames"].is_array()
        ? &document.root()["frames"] : nullptr;
    if (frames == nullptr || frameIndex >= frames->size() || !(*frames)[frameIndex].is_object())
    {
        widget_.cancel_drag();
        ImGui::TextDisabled("The selected animation frame is unavailable.");
        return false;
    }
    if (!animation_texture_is_compatible(assetRoot, textureReference))
    {
        ImGui::TextDisabled("Assign an existing image inside the configured asset folder first.");
        widget_.cancel_drag();
        return false;
    }

    const Json &frame = (*frames)[frameIndex];
    SpriteSourceRegion region{0.0, 0.0, static_cast<double>(sourceWidth),
                              static_cast<double>(sourceHeight)};
    const auto hasRect = frame.find("hasSourceRect");
    const auto rect = frame.find("sourceRect");
    if (hasRect != frame.end() && hasRect->is_boolean() && hasRect->get<bool>() &&
        rect != frame.end() && rect->is_object())
    {
        const auto read = [&](const char *key, double &value) {
            const auto found = rect->find(key);
            if (found == rect->end() || !found->is_number())
                return false;
            value = found->get<double>();
            return std::isfinite(value);
        };
        SpriteSourceRegion stored;
        std::string ignored;
        if (read("x", stored.x) && read("y", stored.y) && read("w", stored.width) &&
            read("h", stored.height) &&
            validate_sprite_source_region(stored, sourceWidth, sourceHeight, ignored))
            region = stored;
    }

    return widget_.draw(
        "##animation-frame-source-region",
        preparedThumbnail, sourceWidth, sourceHeight, region,
        [&](std::string_view field) {
            return frame_region_numeric_address(document, frameIndex, assetRoot,
                                                textureReference, field);
        },
        [&](const SpriteSourceRegion &candidate) -> SpriteSourceRegionWidget::CommitOutcome {
            const auto result = commit_animation_frame_source_region(
                {document, frameIndex}, textureReference, assetRoot, candidate,
                sourceWidth, sourceHeight);
            return {result.changed,
                    result.error};
        },
        false, section);
}
} // namespace platformator_editor
