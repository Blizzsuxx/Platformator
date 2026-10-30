#include "viewport.h"
#include "actions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

#include <imgui.h>

namespace platformator_editor
{
namespace
{
const Json *resolve_object(const DocumentSession &document, int objectId)
{
    const auto location = document.indexes.objectsById.find(objectId);
    if (location == document.indexes.objectsById.end())
        return nullptr;
    const Json *record = nullptr;
    const Json *siblings = &document.root;
    for (std::size_t depth = 0; depth < location->second.childIndices.size(); ++depth)
    {
        if (depth > 0)
        {
            if (record == nullptr)
                return nullptr;
            const auto children = record->find("children");
            if (children == record->end())
                return nullptr;
            siblings = &*children;
        }
        const auto index = location->second.childIndices[depth];
        if (!siblings->is_array() || index >= siblings->size())
            return nullptr;
        record = &(*siblings)[index];
    }
    return record;
}

float number(const Json &record, const char *key, float fallback = 0.0f)
{
    const auto found = record.find(key);
    if (found == record.end() || !found->is_number())
        return fallback;
    try
    {
        const float value = found->get<float>();
        return std::isfinite(value) ? value : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

ViewportPoint position_of(const Json &object)
{
    const auto position = object.find("position");
    if (position == object.end() || !position->is_object())
        return {};
    return {number(*position, "x"), number(*position, "y")};
}

ViewportPoint scale_of(const Json &object)
{
    const auto scale = object.find("scale");
    if (scale == object.end() || !scale->is_object())
        return {1.0f, 1.0f};
    return {number(*scale, "x", 1.0f), number(*scale, "y", 1.0f)};
}

float rotation_of(const Json &object)
{
    return number(object, "rotation");
}

ViewportPoint parent_translation(const DocumentSession &document, int objectId)
{
    ViewportPoint result{};
    int currentId = objectId;
    while (true)
    {
        const auto parent = document.indexes.parentById.find(currentId);
        if (parent == document.indexes.parentById.end() || !parent->second)
            break;
        currentId = *parent->second;
        if (const Json *record = resolve_object(document, currentId))
        {
            const auto position = position_of(*record);
            result.x += position.x;
            result.y += position.y;
        }
    }
    return result;
}

ViewportPoint object_world_position(const DocumentSession &document, int objectId)
{
    const Json *record = resolve_object(document, objectId);
    if (record == nullptr)
        return {};
    ViewportPoint result = position_of(*record);
    const auto parent = parent_translation(document, objectId);
    result.x += parent.x;
    result.y += parent.y;
    return result;
}

const Json *component_of_type(const Json &object, int type)
{
    const auto components = object.find("components");
    if (components == object.end() || !components->is_array())
        return nullptr;
    for (const auto &component : *components)
        if (component.is_object() && component.value("type", -1) == type)
            return &component;
    return nullptr;
}

ViewportPoint rotate(ViewportPoint point, float degrees)
{
    const float radians = degrees * std::numbers::pi_v<float> / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine};
}

bool in_rect(ViewportPoint point, ViewportRect rect)
{
    return point.x >= rect.x && point.y >= rect.y && point.x <= rect.x + rect.width &&
           point.y <= rect.y + rect.height;
}

float distance_squared(ViewportPoint a, ViewportPoint b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float distance_to_segment_squared(ViewportPoint point, ViewportPoint start, ViewportPoint end)
{
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0001f)
        return distance_squared(point, start);
    const float t = std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) /
                                   lengthSquared,
                               0.0f, 1.0f);
    return distance_squared(point, {start.x + t * dx, start.y + t * dy});
}

ViewportPoint normalized(ViewportPoint point)
{
    const float length = std::sqrt(point.x * point.x + point.y * point.y);
    if (!std::isfinite(length) || length <= 0.0001f)
        return {};
    return {point.x / length, point.y / length};
}

struct TransformHandleHit
{
    SceneGestureKind kind = SceneGestureKind::Position;
    ViewportPoint axisWorld{};
    ViewportPoint axisScreen{};
    int scaleAxis = -1;
};

std::optional<TransformHandleHit> hit_transform_handle(const ViewportTransform &transform,
                                                       ViewportPoint pivotWorld,
                                                       float rotationDegrees,
                                                       ViewportPoint mouse)
{
    constexpr float moveLength = 52.0f;
    constexpr float scaleLength = 78.0f;
    constexpr float rotateRadius = 102.0f;
    const ViewportPoint center = transform.world_to_screen(pivotWorld);
    const ViewportPoint rotatedX = rotate({1.0f, 0.0f}, rotationDegrees);
    const ViewportPoint rotatedY = rotate({0.0f, 1.0f}, rotationDegrees);
    const std::array<ViewportPoint, 2> moveAxes{{{1.0f, 0.0f}, {0.0f, 1.0f}}};
    const std::array<ViewportPoint, 2> scaleAxes{{rotatedX, rotatedY}};

    for (int axisIndex = 0; axisIndex < 2; ++axisIndex)
    {
        const ViewportPoint axis = scaleAxes[static_cast<std::size_t>(axisIndex)];
        const ViewportPoint screenAxis = normalized(axis);
        const ViewportPoint end{center.x + screenAxis.x * scaleLength,
                                center.y + screenAxis.y * scaleLength};
        if (distance_squared(mouse, end) <= 13.0f * 13.0f)
            return TransformHandleHit{SceneGestureKind::Scale, axis, screenAxis, axisIndex};
    }

    const float distance = std::sqrt(distance_squared(mouse, center));
    if (std::abs(distance - rotateRadius) <= 10.0f)
        return TransformHandleHit{SceneGestureKind::Rotation, {}, {}, -1};

    for (const auto axis : moveAxes)
    {
        const ViewportPoint screenAxis = normalized(axis);
        const ViewportPoint start{center.x + screenAxis.x * 13.0f,
                                  center.y + screenAxis.y * 13.0f};
        const ViewportPoint end{center.x + screenAxis.x * moveLength,
                                center.y + screenAxis.y * moveLength};
        if (distance_to_segment_squared(mouse, start, end) <= 9.0f * 9.0f)
            return TransformHandleHit{SceneGestureKind::Position, axis, screenAxis, -1};
    }
    return std::nullopt;
}

void draw_transform_gizmo(ImDrawList *drawList, const ViewportTransform &transform, ViewportPoint pivotWorld,
                          float rotationDegrees)
{
    if (drawList == nullptr)
        return;
    constexpr float moveLength = 52.0f;
    constexpr float scaleLength = 78.0f;
    constexpr float rotateRadius = 102.0f;
    const ViewportPoint center = transform.world_to_screen(pivotWorld);
    const ViewportPoint rotatedX = rotate({1.0f, 0.0f}, rotationDegrees);
    const ViewportPoint rotatedY = rotate({0.0f, 1.0f}, rotationDegrees);
    const std::array<ViewportPoint, 2> moveAxes{{{1.0f, 0.0f}, {0.0f, 1.0f}}};
    const std::array<ViewportPoint, 2> scaleAxes{{rotatedX, rotatedY}};
    const std::array<ImU32, 2> axisColors{IM_COL32(235, 85, 75, 255), IM_COL32(95, 205, 125, 255)};

    drawList->AddCircle({center.x, center.y}, rotateRadius, IM_COL32(245, 190, 75, 235), 72, 2.5f);
    const float angle = rotationDegrees * std::numbers::pi_v<float> / 180.0f;
    drawList->AddCircleFilled({center.x + std::cos(angle) * rotateRadius,
                               center.y + std::sin(angle) * rotateRadius},
                              6.0f, IM_COL32(255, 220, 115, 255));

    for (std::size_t index = 0; index < scaleAxes.size(); ++index)
    {
        const ViewportPoint axis = normalized(scaleAxes[index]);
        const ViewportPoint start{center.x + axis.x * (moveLength + 6.0f),
                                  center.y + axis.y * (moveLength + 6.0f)};
        const ViewportPoint end{center.x + axis.x * scaleLength,
                                center.y + axis.y * scaleLength};
        drawList->AddLine({start.x, start.y}, {end.x, end.y}, IM_COL32(95, 165, 245, 255), 3.0f);
        drawList->AddRectFilled({end.x - 6.0f, end.y - 6.0f}, {end.x + 6.0f, end.y + 6.0f},
                                IM_COL32(125, 190, 255, 255), 1.5f);
    }

    for (std::size_t index = 0; index < moveAxes.size(); ++index)
    {
        const ViewportPoint axis = normalized(moveAxes[index]);
        const ViewportPoint end{center.x + axis.x * moveLength,
                                center.y + axis.y * moveLength};
        const ViewportPoint perpendicular{-axis.y, axis.x};
        drawList->AddLine({center.x, center.y}, {end.x, end.y}, axisColors[index], 3.0f);
        const ImVec2 tip{end.x + axis.x * 7.0f, end.y + axis.y * 7.0f};
        drawList->AddTriangleFilled(tip,
            {end.x - axis.x * 6.0f + perpendicular.x * 5.0f,
             end.y - axis.y * 6.0f + perpendicular.y * 5.0f},
            {end.x - axis.x * 6.0f - perpendicular.x * 5.0f,
             end.y - axis.y * 6.0f - perpendicular.y * 5.0f}, axisColors[index]);
    }
    drawList->AddCircleFilled({center.x, center.y}, 4.5f, IM_COL32(240, 240, 245, 255));
}

std::array<ViewportPoint, 4> box_corners(ViewportPoint center, float width, float height, float rotation)
{
    std::array<ViewportPoint, 4> result{{{-width * 0.5f, -height * 0.5f},
                                         {width * 0.5f, -height * 0.5f},
                                         {width * 0.5f, height * 0.5f},
                                         {-width * 0.5f, height * 0.5f}}};
    for (auto &corner : result)
    {
        corner = rotate(corner, rotation);
        corner.x += center.x;
        corner.y += center.y;
    }
    return result;
}

ImVec2 imgui_point(ViewportPoint point)
{
    return {point.x, point.y};
}

void draw_world_box(ImDrawList *drawList, const ViewportTransform &transform,
                    ViewportPoint center, float width, float height, float rotation,
                    ImU32 color, float thickness = 1.0f)
{
    const auto corners = box_corners(center, width, height, rotation);
    ImVec2 points[4];
    for (int index = 0; index < 4; ++index)
        points[index] = imgui_point(transform.world_to_screen(corners[static_cast<std::size_t>(index)]));
    drawList->AddPolyline(points, 4, color, ImDrawFlags_Closed, thickness);
}

struct CameraDefaults
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 1920.0f;
    float height = 1080.0f;
};

CameraDefaults document_camera(const DocumentSession &document)
{
    CameraDefaults result;
    for (const int id : document.indexes.orderedObjectIds)
    {
        const Json *object = resolve_object(document, id);
        if (object == nullptr)
            continue;
        const Json *camera = component_of_type(*object, 2);
        if (camera == nullptr)
            continue;
        const auto position = position_of(*object);
        const auto parents = parent_translation(document, id);
        result.x = position.x + parents.x;
        result.y = position.y + parents.y;
        result.width = std::max(1.0f, number(*camera, "width", result.width));
        result.height = std::max(1.0f, number(*camera, "height", result.height));
        return result;
    }
    return result;
}

ViewportRect bounds_for_ids(const std::vector<int> &ids, const EditorPreview &preview)
{
    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    for (int id : ids)
    {
        const auto *info = preview.object_info(id);
        if (info == nullptr)
            continue;
        const float halfWidth = std::abs(info->width) * 0.5f;
        const float halfHeight = std::abs(info->height) * 0.5f;
        minX = std::min(minX, info->worldX - halfWidth);
        minY = std::min(minY, info->worldY - halfHeight);
        maxX = std::max(maxX, info->worldX + halfWidth);
        maxY = std::max(maxY, info->worldY + halfHeight);
    }
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY))
        return {};
    if (maxX - minX < 1.0f)
        maxX = minX + 1.0f;
    if (maxY - minY < 1.0f)
        maxY = minY + 1.0f;
    return {minX, minY, maxX - minX, maxY - minY};
}
} // namespace

bool ViewportTransform::configure(ViewportRect imageRect, float framebufferScaleX, float framebufferScaleY,
                                  float baseWorldWidth, float baseWorldHeight,
                                  float centerX, float centerY, float zoom)
{
    imageRect_ = imageRect;
    framebufferScaleX_ = framebufferScaleX;
    framebufferScaleY_ = framebufferScaleY;
    baseWorldWidth_ = baseWorldWidth;
    baseWorldHeight_ = baseWorldHeight;
    centerX_ = centerX;
    centerY_ = centerY;
    zoom_ = std::clamp(zoom, 0.05f, 64.0f);
    valid_ = std::isfinite(imageRect.x) && std::isfinite(imageRect.y) &&
             std::isfinite(imageRect.width) && std::isfinite(imageRect.height) &&
             imageRect.width > 0.0f && imageRect.height > 0.0f &&
             std::isfinite(framebufferScaleX) && framebufferScaleX > 0.0f &&
             std::isfinite(framebufferScaleY) && framebufferScaleY > 0.0f &&
             std::isfinite(baseWorldWidth) && baseWorldWidth > 0.0f &&
             std::isfinite(baseWorldHeight) && baseWorldHeight > 0.0f &&
             std::isfinite(centerX) && std::isfinite(centerY) && std::isfinite(zoom_);
    if (valid_)
        update_projection();
    else
    {
        contentRect_ = {};
        targetWidth_ = targetHeight_ = 0;
    }
    return valid_;
}

void ViewportTransform::update_projection() noexcept
{
    contentRect_ = imageRect_;
    worldHeight_ = baseWorldHeight_ / zoom_;
    worldWidth_ = worldHeight_ * contentRect_.width / contentRect_.height;
    cameraLeft_ = centerX_ - worldWidth_ * 0.5f;
    cameraTop_ = centerY_ - worldHeight_ * 0.5f;
    targetWidth_ = std::max(1, static_cast<int>(std::lround(contentRect_.width * framebufferScaleX_)));
    targetHeight_ = std::max(1, static_cast<int>(std::lround(contentRect_.height * framebufferScaleY_)));
}

bool ViewportTransform::valid() const noexcept { return valid_; }
const ViewportRect &ViewportTransform::image_rect() const noexcept { return imageRect_; }
const ViewportRect &ViewportTransform::content_rect() const noexcept { return contentRect_; }
int ViewportTransform::target_width() const noexcept { return valid_ ? targetWidth_ : 0; }
int ViewportTransform::target_height() const noexcept { return valid_ ? targetHeight_ : 0; }
float ViewportTransform::camera_left() const noexcept { return cameraLeft_; }
float ViewportTransform::camera_top() const noexcept { return cameraTop_; }
float ViewportTransform::world_width() const noexcept { return worldWidth_; }
float ViewportTransform::world_height() const noexcept { return worldHeight_; }
float ViewportTransform::zoom() const noexcept { return zoom_; }

std::optional<ViewportPoint> ViewportTransform::screen_to_world(ViewportPoint point) const noexcept
{
    if (!valid_ || !in_rect(point, contentRect_))
        return std::nullopt;
    const float u = (point.x - contentRect_.x) / contentRect_.width;
    const float v = (point.y - contentRect_.y) / contentRect_.height;
    return ViewportPoint{cameraLeft_ + u * worldWidth_, cameraTop_ + v * worldHeight_};
}

ViewportPoint ViewportTransform::world_to_screen(ViewportPoint point) const noexcept
{
    if (!valid_)
        return {};
    return {contentRect_.x + (point.x - cameraLeft_) / worldWidth_ * contentRect_.width,
            contentRect_.y + (point.y - cameraTop_) / worldHeight_ * contentRect_.height};
}

std::optional<int> ViewportPickCycle::choose(const std::vector<int> &candidates, ViewportPoint screenPoint)
{
    if (candidates.empty())
    {
        reset();
        return std::nullopt;
    }
    const bool repeated = hasPrevious_ && candidates == previousCandidates_ &&
                          distance_squared(screenPoint, previousPoint_) <= 49.0f;
    if (repeated)
        index_ = (index_ + 1) % candidates.size();
    else
        index_ = 0;
    previousCandidates_ = candidates;
    previousPoint_ = screenPoint;
    hasPrevious_ = true;
    return candidates[index_];
}

void ViewportPickCycle::reset() noexcept
{
    previousCandidates_.clear();
    index_ = 0;
    hasPrevious_ = false;
}

void ViewportTransform::pan(float logicalDeltaX, float logicalDeltaY) noexcept
{
    if (!valid_ || !std::isfinite(logicalDeltaX) || !std::isfinite(logicalDeltaY))
        return;
    centerX_ -= logicalDeltaX / contentRect_.width * worldWidth_;
    centerY_ -= logicalDeltaY / contentRect_.height * worldHeight_;
    update_projection();
}

bool ViewportTransform::zoom(float factor)
{
    if (!valid_ || !std::isfinite(factor) || factor <= 0.0f)
        return false;
    zoom_ = std::clamp(zoom_ * factor, 0.05f, 64.0f);
    update_projection();
    return true;
}

bool ViewportTransform::zoom_at(ViewportPoint screenPoint, float factor)
{
    const auto anchor = screen_to_world(screenPoint);
    if (!anchor || !std::isfinite(factor) || factor <= 0.0f)
        return false;
    zoom_ = std::clamp(zoom_ * factor, 0.05f, 64.0f);
    worldWidth_ = baseWorldWidth_ / zoom_;
    worldHeight_ = baseWorldHeight_ / zoom_;
    const float u = (screenPoint.x - contentRect_.x) / contentRect_.width;
    const float v = (screenPoint.y - contentRect_.y) / contentRect_.height;
    centerX_ = anchor->x + worldWidth_ * (0.5f - u);
    centerY_ = anchor->y + worldHeight_ * (0.5f - v);
    update_projection();
    return true;
}

bool ViewportTransform::frame_bounds(ViewportRect worldBounds, float padding)
{
    if (!valid_ || worldBounds.width <= 0.0f || worldBounds.height <= 0.0f ||
        !std::isfinite(worldBounds.width) || !std::isfinite(worldBounds.height) ||
        !std::isfinite(padding) || padding < 1.0f)
        return false;
    const float targetWorldWidth = worldBounds.width * padding;
    const float targetWorldHeight = worldBounds.height * padding;
    const float aspect = imageRect_.width / imageRect_.height;
    const float fitWidth = baseWorldHeight_ * aspect / targetWorldWidth;
    const float fitHeight = baseWorldHeight_ / targetWorldHeight;
    zoom_ = std::clamp(std::min(fitWidth, fitHeight), 0.05f, 64.0f);
    centerX_ = worldBounds.x + worldBounds.width * 0.5f;
    centerY_ = worldBounds.y + worldBounds.height * 0.5f;
    update_projection();
    return true;
}

std::vector<int> viewport_pick_candidates(const ViewportTransform &transform,
                                          const std::vector<int> &orderedDocumentIds,
                                          const EditorPreview &preview, ViewportPoint screenPoint,
                                          float tolerancePixels)
{
    const auto worldPoint = transform.screen_to_world(screenPoint);
    if (!worldPoint || !std::isfinite(tolerancePixels) || tolerancePixels < 0.0f)
        return {};
    const float toleranceWorld = tolerancePixels * std::max(
        transform.world_width() / transform.content_rect().width,
        transform.world_height() / transform.content_rect().height);
    std::vector<int> result;
    for (auto iter = orderedDocumentIds.rbegin(); iter != orderedDocumentIds.rend(); ++iter)
    {
        const PreviewObjectInfo *info = preview.object_info(*iter);
        if (info == nullptr || !info->hasSprite)
            continue;
        const ViewportPoint objectCenter{info->worldX, info->worldY};
        const float objectRotation = info->rotationDegrees;
        ViewportPoint local = rotate({worldPoint->x - objectCenter.x, worldPoint->y - objectCenter.y}, -objectRotation);
        bool hit = std::abs(local.x) <= std::abs(info->width) * 0.5f + toleranceWorld &&
                   std::abs(local.y) <= std::abs(info->height) * 0.5f + toleranceWorld;
        if (!hit && info->colliderShape != PreviewColliderShape::None)
        {
            const auto offset = rotate({info->colliderOffsetX, info->colliderOffsetY}, objectRotation);
            const ViewportPoint colliderCenter{objectCenter.x + offset.x, objectCenter.y + offset.y};
            local = rotate({worldPoint->x - colliderCenter.x, worldPoint->y - colliderCenter.y}, -objectRotation);
            if (info->colliderShape == PreviewColliderShape::Circle)
            {
                const float radius = info->colliderRadius + toleranceWorld;
                hit = local.x * local.x + local.y * local.y <= radius * radius;
            }
            else
                hit = std::abs(local.x) <= info->colliderWidth * 0.5f + toleranceWorld &&
                      std::abs(local.y) <= info->colliderHeight * 0.5f + toleranceWorld;
        }
        if (hit)
            result.push_back(*iter);
    }
    return result;
}

std::optional<ViewportPoint> object_local_position_for_world(const DocumentSession &document,
                                                             int objectId, ViewportPoint worldPosition)
{
    if (resolve_object(document, objectId) == nullptr)
        return std::nullopt;
    const auto parents = parent_translation(document, objectId);
    return ViewportPoint{worldPosition.x - parents.x, worldPosition.y - parents.y};
}

std::optional<ViewportPoint> collider_offset_for_world(const DocumentSession &document,
                                                       int objectId, ViewportPoint colliderWorldPosition)
{
    const Json *object = resolve_object(document, objectId);
    if (object == nullptr)
        return std::nullopt;
    const auto center = object_world_position(document, objectId);
    const float rotation = number(*object, "rotation") * 180.0f / std::numbers::pi_v<float>;
    return rotate({colliderWorldPosition.x - center.x, colliderWorldPosition.y - center.y}, -rotation);
}

std::optional<PropertyGesture> begin_object_position_gesture(EditCommandService &commands,
                                                             const DocumentSession &document, int objectId,
                                                             SelectionState selection)
{
    if (resolve_object(document, objectId) == nullptr)
        return std::nullopt;
    PropertyAddress address{document.documentGeneration, objectId, std::nullopt, std::nullopt,
                            {std::string("position")}};
    return commands.begin_gesture(document, address, selection);
}

std::optional<PropertyGesture> begin_collider_offset_gesture(EditCommandService &commands,
                                                            const DocumentSession &document, int objectId,
                                                            SelectionState selection)
{
    const Json *object = resolve_object(document, objectId);
    const Json *collider = object == nullptr ? nullptr : component_of_type(*object, 3);
    if (collider == nullptr)
        return std::nullopt;
    const auto componentId = collider->value("id", -1);
    if (componentId < 0)
        return std::nullopt;
    PropertyAddress address{document.documentGeneration, objectId, componentId, std::nullopt,
                            {std::string("offset")}};
    return commands.begin_gesture(document, address, selection);
}

void SceneViewportPanel::cancel_gesture(EditCommandService &commands, DocumentSession &document)
{
    if (gesture_ && gesture_->active())
        gesture_->cancel(commands, document);
    gesture_.reset();
    pendingDrag_ = false;
    panning_ = false;
}

void SceneViewportPanel::commit_gesture()
{
    if (gesture_)
    {
        gesture_->commit();
        gesture_.reset();
    }
    pendingDrag_ = false;
    panning_ = false;
}

void SceneViewportPanel::set_visible(bool visible) noexcept { open_ = visible; }
bool SceneViewportPanel::is_visible() const noexcept { return open_; }

void SceneViewportPanel::adjust_zoom(float factor) noexcept
{
    if (std::isfinite(factor) && factor > 0.0f)
        pendingZoomFactor_ = std::clamp(pendingZoomFactor_ * factor, 0.05f, 64.0f);
}

void SceneViewportPanel::request_frame_selected() noexcept { frameSelectedRequested_ = true; }
void SceneViewportPanel::request_frame_all() noexcept { frameAllRequested_ = true; }

bool SceneViewportPanel::gesture_active() const noexcept
{
    return gesture_ && gesture_->active();
}

bool SceneViewportPanel::has_active_input() const noexcept
{
    return gesture_active() || pendingDrag_ || panning_;
}

void SceneViewportPanel::draw(DocumentSession &document, EditorPreview &preview, SelectionModel &selection,
                              EditCommandService &commands, SDL_Window *window, SDL_Renderer *renderer,
                              bool modalOrTextCapture, bool editingEnabled)
{
    if (!open_)
    {
        cancel_gesture(commands, document);
        return;
    }
    if (!ImGui::Begin("Scene###panel.scene", &open_,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar))
    {
        cancel_gesture(commands, document);
        ImGui::End();
        return;
    }
    const CameraDefaults camera = document_camera(document);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (!hasView_)
    {
        viewCenter_ = {camera.x + camera.width * 0.5f, camera.y + camera.height * 0.5f};
        if (available.x > 0.0f && available.y > 0.0f && camera.width > 0.0f && camera.height > 0.0f)
            viewZoom_ = std::min(1.0f, (available.x / available.y) / (camera.width / camera.height));
        hasView_ = true;
    }
    if (pendingZoomFactor_ != 1.0f)
    {
        viewZoom_ = std::clamp(viewZoom_ * pendingZoomFactor_, 0.05f, 64.0f);
        pendingZoomFactor_ = 1.0f;
    }
    bool frameAll = ImGui::Button("Frame All") || frameAllRequested_;
    ImGui::SameLine();
    bool frameSelected = ImGui::Button("Frame Selected") || frameSelectedRequested_;
    frameAllRequested_ = false;
    frameSelectedRequested_ = false;
    ImGui::SameLine();
    ImGui::Text("Zoom %.0f%%", viewZoom_ * 100.0f);
    ImGui::SetItemTooltip("Drag red/green arrows to move, blue squares to scale, or the gold ring to rotate. Drag the object itself to move freely.");
    if (!document.sourcePath)
        ImGui::TextDisabled("No authored scene is loaded. Use Open Scene or New Scene to begin.");
    const ImVec2 viewportAvailable = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ViewportRect imageRect{origin.x, origin.y, std::max(0.0f, viewportAvailable.x),
                                 std::max(0.0f, viewportAvailable.y)};
    int outputWidth = 0;
    int outputHeight = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    const bool haveOutput = renderer != nullptr && SDL_GetRenderOutputSize(renderer, &outputWidth, &outputHeight);
    const bool haveWindow = window != nullptr && SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    const float pixelRatioX = haveOutput && haveWindow && windowWidth > 0
                                  ? static_cast<float>(outputWidth) / windowWidth : 1.0f;
    const float pixelRatioY = haveOutput && haveWindow && windowHeight > 0
                                  ? static_cast<float>(outputHeight) / windowHeight : 1.0f;
    const bool transformReady = transform_.configure(imageRect, pixelRatioX, pixelRatioY,
                                                       camera.width, camera.height,
                                                       viewCenter_.x, viewCenter_.y, viewZoom_);
    if (frameAll)
    {
        ViewportRect bounds = bounds_for_ids(preview.ordered_document_ids(), preview);
        if (bounds.width <= 0.0f || bounds.height <= 0.0f)
            bounds = {camera.x, camera.y, camera.width, camera.height};
        if (transformReady && transform_.frame_bounds(bounds))
        {
            viewZoom_ = transform_.zoom();
            viewCenter_ = {transform_.camera_left() + transform_.world_width() * 0.5f,
                           transform_.camera_top() + transform_.world_height() * 0.5f};
        }
    }
    else if (frameSelected && selection.current().objectId)
    {
        const std::vector<int> selected{*selection.current().objectId};
        const ViewportRect bounds = bounds_for_ids(selected, preview);
        if (transformReady && bounds.width > 0.0f && bounds.height > 0.0f && transform_.frame_bounds(bounds))
        {
            viewZoom_ = transform_.zoom();
            viewCenter_ = {transform_.camera_left() + transform_.world_width() * 0.5f,
                           transform_.camera_top() + transform_.world_height() * 0.5f};
        }
    }

    const ViewportRect content = transform_.content_rect();
    ImGui::SetCursorScreenPos({content.x, content.y});
    if (transformReady && preview.render(transform_.camera_left(), transform_.camera_top(),
                                         transform_.world_width(), transform_.world_height(),
                                         transform_.target_width(), transform_.target_height()) &&
        preview.target() != nullptr)
    {
        const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(preview.target()));
        ImGui::Image(ImTextureRef(textureId), ImVec2(content.width, content.height));
    }
    else
        ImGui::Dummy(ImVec2(std::max(content.width, 1.0f), std::max(content.height, 1.0f)));
    bool imageHovered = false;
    if (transformReady && content.width > 0.0f && content.height > 0.0f)
    {
        ImGui::SetCursorScreenPos({content.x, content.y});
        ImGui::InvisibleButton("##scene-viewport-input", {content.width, content.height});
        imageHovered = ImGui::IsItemHovered();
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect({content.x, content.y}, {content.x + content.width, content.y + content.height}, true);

    if (transformReady)
    {
        constexpr float gridSpacing = 100.0f;
        const float firstX = std::floor(transform_.camera_left() / gridSpacing) * gridSpacing;
        const float firstY = std::floor(transform_.camera_top() / gridSpacing) * gridSpacing;
        for (float x = firstX; x <= transform_.camera_left() + transform_.world_width() &&
                                x - firstX < gridSpacing * 128.0f; x += gridSpacing)
        {
            const auto point = transform_.world_to_screen({x, transform_.camera_top()});
            drawList->AddLine({point.x, content.y}, {point.x, content.y + content.height},
                              x == 0.0f ? IM_COL32(90, 112, 125, 140) : IM_COL32(70, 75, 82, 75), 1.0f);
        }
        for (float y = firstY; y <= transform_.camera_top() + transform_.world_height() &&
                                y - firstY < gridSpacing * 128.0f; y += gridSpacing)
        {
            const auto point = transform_.world_to_screen({transform_.camera_left(), y});
            drawList->AddLine({content.x, point.y}, {content.x + content.width, point.y},
                              y == 0.0f ? IM_COL32(90, 112, 125, 140) : IM_COL32(70, 75, 82, 75), 1.0f);
        }
        drawList->AddRect({content.x, content.y}, {content.x + content.width, content.y + content.height},
                          IM_COL32(100, 105, 112, 220));
        draw_world_box(drawList, transform_, {camera.x + camera.width * 0.5f, camera.y + camera.height * 0.5f},
                       camera.width, camera.height, 0.0f, IM_COL32(115, 190, 130, 210), 1.5f);
        const auto selectedId = selection.current().objectId;
        for (const int id : preview.ordered_document_ids())
        {
            const PreviewObjectInfo *info = preview.object_info(id);
            if (info == nullptr)
                continue;
            const bool selected = selectedId && *selectedId == id;
            const ImU32 spriteColor = selected ? IM_COL32(255, 210, 75, 255)
                                   : !info->textureAvailable ? IM_COL32(240, 105, 90, 200)
                                   : !info->active ? IM_COL32(135, 145, 155, 140)
                                                   : IM_COL32(110, 185, 210, 95);
            draw_world_box(drawList, transform_, {info->worldX, info->worldY}, info->width, info->height,
                           info->rotationDegrees, spriteColor, selected ? 2.0f : 1.0f);
            if (info->colliderShape != PreviewColliderShape::None)
            {
                const auto offset = rotate({info->colliderOffsetX, info->colliderOffsetY}, info->rotationDegrees);
                const ViewportPoint colliderCenter{info->worldX + offset.x, info->worldY + offset.y};
                if (info->colliderShape == PreviewColliderShape::Circle)
                {
                    const auto screenCenter = transform_.world_to_screen(colliderCenter);
                    const float radius = info->colliderRadius * content.width / transform_.world_width();
                    drawList->AddCircle({screenCenter.x, screenCenter.y}, radius,
                                        selected ? IM_COL32(245, 145, 65, 255) : IM_COL32(235, 145, 75, 185));
                }
                else
                    draw_world_box(drawList, transform_, colliderCenter, info->colliderWidth,
                                   info->colliderHeight, info->rotationDegrees,
                                   selected ? IM_COL32(245, 145, 65, 255) : IM_COL32(235, 145, 75, 185));
                if (selected)
                {
                    const auto handle = transform_.world_to_screen(colliderCenter);
                    drawList->AddCircleFilled({handle.x, handle.y}, 4.0f, IM_COL32(255, 170, 75, 255));
                }
            }
            if (selected)
            {
                const auto handle = transform_.world_to_screen({info->worldX, info->worldY});
                drawList->AddCircleFilled({handle.x, handle.y}, 4.0f, IM_COL32(255, 220, 80, 255));
            }
        }
    }
    if (transformReady && selection.current().objectId)
    {
        if (const Json *selectedObject = resolve_object(document, *selection.current().objectId))
        {
            const float rotationDegrees = rotation_of(*selectedObject) * 180.0f /
                                          std::numbers::pi_v<float>;
            draw_transform_gizmo(drawList, transform_,
                                 object_world_position(document, *selection.current().objectId),
                                 rotationDegrees);
        }
    }
    drawList->PopClipRect();

    ImGuiIO &io = ImGui::GetIO();
    const ViewportPoint mouse{io.MousePos.x, io.MousePos.y};
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool pressedOnScene = imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool capture = modalOrTextCapture || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
                         (io.WantTextInput && !pressedOnScene);
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool appHasFocus = window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const EditorInputCapture sceneCapture{appHasFocus, capture, false, has_active_input(), focused};
    if (ImGui::IsKeyPressed(ImGuiKey_F))
    {
        const auto frameAction = dispatch_editor_key(EditorKey::F, io.KeyShift, io.KeyCtrl, sceneCapture);
        if (frameAction == EditorAction::FrameSelected)
            frameSelectedRequested_ = true;
        else if (frameAction == EditorAction::FrameAll)
            frameAllRequested_ = true;
    }
    if (!editingEnabled)
    {
        cancel_gesture(commands, document);
        pendingDrag_ = false;
    }
    if (!transformReady || !appHasFocus)
        cancel_gesture(commands, document);
    if ((gesture_ && (capture || !appHasFocus || !selection.current().objectId ||
                      gestureAddress_.documentGeneration != document.documentGeneration ||
                      *selection.current().objectId != gestureAddress_.objectId || escape)) ||
        (pendingDrag_ && (capture || !appHasFocus || escape ||
                          !selection.current().objectId ||
                          *selection.current().objectId != pendingDragObjectId_)))
        cancel_gesture(commands, document);

    const auto world_at_screen = [&](ViewportPoint point) {
        if (!transformReady || content.width <= 0.0f || content.height <= 0.0f)
            return ViewportPoint{};
        return ViewportPoint{transform_.camera_left() + (point.x - content.x) / content.width * transform_.world_width(),
                             transform_.camera_top() + (point.y - content.y) / content.height * transform_.world_height()};
    };
    if (pendingDrag_)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            pendingDrag_ = false;
        else if (distance_squared(mouse, pendingPressScreen_) >= 9.0f)
        {
            const Selection currentSelection = selection.current();
            const SelectionState selectionState{currentSelection, currentSelection};
            switch (pendingGestureKind_)
            {
            case SceneGestureKind::Position:
                gesture_ = begin_object_position_gesture(commands, document, pendingDragObjectId_,
                                                        selectionState);
                break;
            case SceneGestureKind::ColliderOffset:
                gesture_ = begin_collider_offset_gesture(commands, document, pendingDragObjectId_,
                                                        selectionState);
                break;
            case SceneGestureKind::Scale:
                gesture_ = commands.begin_gesture(document,
                    PropertyAddress{document.documentGeneration, pendingDragObjectId_, std::nullopt,
                                    std::nullopt, {std::string("scale")}}, selectionState);
                break;
            case SceneGestureKind::Rotation:
                gesture_ = commands.begin_gesture(document,
                    PropertyAddress{document.documentGeneration, pendingDragObjectId_, std::nullopt,
                                    std::nullopt, {std::string("rotation")}}, selectionState);
                break;
            }
            if (gesture_)
            {
                gestureAddress_.documentGeneration = document.documentGeneration;
                gestureAddress_.objectId = pendingDragObjectId_;
                gestureKind_ = pendingGestureKind_;
                gesturePointerWorld_ = pendingPointerWorld_;
                gestureStartWorld_ = pendingTargetWorld_;
                gestureAxisWorld_ = pendingAxisWorld_;
                gestureAxisScreen_ = pendingAxisScreen_;
                gestureScaleAxis_ = pendingScaleAxis_;
                gesturePivotScreen_ = pendingPivotScreen_;
                if (const Json *object = resolve_object(document, pendingDragObjectId_))
                {
                    gestureStartScale_ = scale_of(*object);
                    gestureStartRotation_ = rotation_of(*object);
                }
                gestureStartAngle_ = std::atan2(pendingPressScreen_.y - gesturePivotScreen_.y,
                                                pendingPressScreen_.x - gesturePivotScreen_.x);
            }
            pendingDrag_ = false;
        }
    }

    if (gesture_ && gesture_->active())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ViewportPoint currentWorld = world_at_screen(mouse);
            ViewportPoint delta{currentWorld.x - gesturePointerWorld_.x,
                                currentWorld.y - gesturePointerWorld_.y};
            Json value;
            switch (gestureKind_)
            {
            case SceneGestureKind::Position:
            {
                const float axisLengthSquared = gestureAxisWorld_.x * gestureAxisWorld_.x +
                                               gestureAxisWorld_.y * gestureAxisWorld_.y;
                if (axisLengthSquared > 0.0001f)
                {
                    const float amount = (delta.x * gestureAxisWorld_.x +
                                          delta.y * gestureAxisWorld_.y) / axisLengthSquared;
                    delta = {gestureAxisWorld_.x * amount, gestureAxisWorld_.y * amount};
                }
                const auto local = object_local_position_for_world(
                    document, gestureAddress_.objectId,
                    {gestureStartWorld_.x + delta.x, gestureStartWorld_.y + delta.y});
                if (local)
                    value = Json{{"x", local->x}, {"y", local->y}};
                break;
            }
            case SceneGestureKind::ColliderOffset:
            {
                const auto localOffset = collider_offset_for_world(
                    document, gestureAddress_.objectId,
                    {gestureStartWorld_.x + delta.x, gestureStartWorld_.y + delta.y});
                if (localOffset)
                    value = Json{{"x", localOffset->x}, {"y", localOffset->y}};
                break;
            }
            case SceneGestureKind::Scale:
            {
                const float pixelDelta = (mouse.x - pendingPressScreen_.x) * gestureAxisScreen_.x +
                                         (mouse.y - pendingPressScreen_.y) * gestureAxisScreen_.y;
                const float factor = 1.0f + pixelDelta / 62.0f;
                const float scaleX = gestureScaleAxis_ == 0
                    ? std::max(0.01f, gestureStartScale_.x * factor) : gestureStartScale_.x;
                const float scaleY = gestureScaleAxis_ == 1
                    ? std::max(0.01f, gestureStartScale_.y * factor) : gestureStartScale_.y;
                value = Json{{"x", scaleX}, {"y", scaleY}};
                break;
            }
            case SceneGestureKind::Rotation:
            {
                const float currentAngle = std::atan2(mouse.y - gesturePivotScreen_.y,
                                                      mouse.x - gesturePivotScreen_.x);
                const float angleDelta = std::remainder(currentAngle - gestureStartAngle_,
                                                        2.0f * std::numbers::pi_v<float>);
                value = gestureStartRotation_ + angleDelta;
                break;
            }
            }
            if (value.is_null())
                cancel_gesture(commands, document);
            else
            {
                const CommandResult update = gesture_->update(commands, document, std::move(value));
                if (!update)
                    cancel_gesture(commands, document);
            }
        }
        else
        {
            gesture_->commit();
            gesture_.reset();
        }
    }

    if (imageHovered && !capture && !gesture_)
    {
        const float wheelStep = std::clamp(io.MouseWheel, -1.0f, 1.0f);
        if (wheelStep != 0.0f && transform_.zoom(std::pow(1.1f, wheelStep)))
            viewZoom_ = transform_.zoom();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            panning_ = true;
            panStart_ = mouse;
        }
        if (panning_ && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right)))
        {
            const float deltaX = mouse.x - panStart_.x;
            const float deltaY = mouse.y - panStart_.y;
            transform_.pan(deltaX, deltaY);
            viewCenter_ = {transform_.camera_left() + transform_.world_width() * 0.5f,
                           transform_.camera_top() + transform_.world_height() * 0.5f};
            panStart_ = mouse;
        }
        else if (panning_)
            panning_ = false;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            Selection selected = selection.current();
            selected.focusedPanel = FocusedPanel::Scene;
            const auto worldPoint = transform_.screen_to_world(mouse);
            bool startedOnGizmo = false;
            if (editingEnabled && selected.objectId)
            {
                if (const Json *object = resolve_object(document, *selected.objectId))
                {
                    const ViewportPoint pivot = object_world_position(document, *selected.objectId);
                    const float rotationDegrees = rotation_of(*object) * 180.0f /
                                                  std::numbers::pi_v<float>;
                    const auto handle = hit_transform_handle(transform_, pivot,
                                                             rotationDegrees, mouse);
                    if (handle)
                    {
                        selection.set(document, selected);
                        pendingGestureKind_ = handle->kind;
                        pendingDragObjectId_ = *selected.objectId;
                        pendingPressScreen_ = mouse;
                        pendingPointerWorld_ = worldPoint.value_or(pivot);
                        pendingTargetWorld_ = pivot;
                        pendingAxisWorld_ = handle->axisWorld;
                        pendingAxisScreen_ = handle->axisScreen;
                        pendingScaleAxis_ = handle->scaleAxis;
                        pendingPivotScreen_ = transform_.world_to_screen(pivot);
                        pendingDrag_ = true;
                        startedOnGizmo = true;
                    }
                }
            }

            if (!startedOnGizmo)
            {
                const auto candidates = viewport_pick_candidates(transform_, preview.ordered_document_ids(),
                                                                  preview, mouse);
                if (candidates.empty())
                {
                    selected.objectId.reset();
                    selected.componentId.reset();
                    selected.behaviorOrdinal.reset();
                    pickCycle_.reset();
                }
                else
                {
                    selected.objectId = pickCycle_.choose(candidates, mouse);
                    selected.componentId.reset();
                    selected.behaviorOrdinal.reset();
                }
                selection.set(document, selected);
                pendingDrag_ = false;
                if (editingEnabled && worldPoint && selected.objectId)
                {
                    const Json *object = resolve_object(document, *selected.objectId);
                    if (object != nullptr)
                    {
                        const ViewportPoint objectWorld = object_world_position(document, *selected.objectId);
                        const PreviewObjectInfo *info = preview.object_info(*selected.objectId);
                        ViewportPoint colliderWorld = objectWorld;
                        bool colliderHandle = false;
                        if (info != nullptr && info->colliderShape != PreviewColliderShape::None)
                        {
                            const ViewportPoint offset = rotate(
                                {info->colliderOffsetX, info->colliderOffsetY}, info->rotationDegrees);
                            colliderWorld = {info->worldX + offset.x, info->worldY + offset.y};
                            const auto colliderScreen = transform_.world_to_screen(colliderWorld);
                            colliderHandle = distance_squared(mouse, colliderScreen) <= 64.0f;
                        }
                        pendingGestureKind_ = colliderHandle
                            ? SceneGestureKind::ColliderOffset : SceneGestureKind::Position;
                        pendingDragObjectId_ = *selected.objectId;
                        pendingPressScreen_ = mouse;
                        pendingPointerWorld_ = *worldPoint;
                        pendingTargetWorld_ = colliderHandle ? colliderWorld : objectWorld;
                        pendingAxisWorld_ = {};
                        pendingAxisScreen_ = {};
                        pendingScaleAxis_ = -1;
                        pendingPivotScreen_ = transform_.world_to_screen(objectWorld);
                        pendingDrag_ = true;
                    }
                }
            }
        }
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        panning_ = false;

    if (!focused || capture)
        panning_ = false;
    ImGui::End();
}
} // namespace platformator_editor

