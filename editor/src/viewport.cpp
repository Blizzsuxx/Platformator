#include "viewport.h"

#include "editorwork.h"

#include "camerainterpretation.h"
#include "actions.h"
#include "assetformats.h"
#include "style.h"
#include "templates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_set>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

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

        std::optional<float> finite_number_property(const Json &record, const char *key)
        {
            const auto found = record.find(key);
            if (found == record.end() || !found->is_number())
                return std::nullopt;
            try
            {
                const float value = found->get<float>();
                return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
            }
            catch (...)
            {
                return std::nullopt;
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
                                                               ViewportPoint mouse,
                                                               SceneTool tool,
                                                               float uiScale)
        {
            const float moveLength = 52.0f * uiScale;
            const float scaleLength = 78.0f * uiScale;
            const float rotateRadius = 102.0f * uiScale;
            const ViewportPoint center = transform.world_to_screen(pivotWorld);
            const ViewportPoint rotatedX = rotate({1.0f, 0.0f}, rotationDegrees);
            const ViewportPoint rotatedY = rotate({0.0f, 1.0f}, rotationDegrees);
            const std::array<ViewportPoint, 2> moveAxes{{{1.0f, 0.0f}, {0.0f, 1.0f}}};
            const std::array<ViewportPoint, 2> scaleAxes{{rotatedX, rotatedY}};

            if (tool == SceneTool::Scale)
            {
                for (int axisIndex = 0; axisIndex < 2; ++axisIndex)
                {
                    const ViewportPoint axis = scaleAxes[static_cast<std::size_t>(axisIndex)];
                    const ViewportPoint screenAxis = normalized(axis);
                    const ViewportPoint end{center.x + screenAxis.x * scaleLength,
                                            center.y + screenAxis.y * scaleLength};
                    const float hitRadius = 13.0f * uiScale;
                    if (distance_squared(mouse, end) <= hitRadius * hitRadius)
                        return TransformHandleHit{SceneGestureKind::Scale, axis, screenAxis, axisIndex};
                }
                return std::nullopt;
            }

            if (tool == SceneTool::Rotate)
            {
                const float distance = std::sqrt(distance_squared(mouse, center));
                if (std::abs(distance - rotateRadius) <= 10.0f * uiScale)
                    return TransformHandleHit{SceneGestureKind::Rotation, {}, {}, -1};
                return std::nullopt;
            }
            if (tool != SceneTool::Move)
                return std::nullopt;

            for (const auto axis : moveAxes)
            {
                const ViewportPoint screenAxis = normalized(axis);
                const ViewportPoint start{center.x + screenAxis.x * 13.0f * uiScale,
                                          center.y + screenAxis.y * 13.0f * uiScale};
                const ViewportPoint end{center.x + screenAxis.x * moveLength,
                                        center.y + screenAxis.y * moveLength};
                const float hitRadius = 9.0f * uiScale;
                if (distance_to_segment_squared(mouse, start, end) <= hitRadius * hitRadius)
                    return TransformHandleHit{SceneGestureKind::Position, axis, screenAxis, -1};
            }
            return std::nullopt;
        }

        void draw_transform_gizmo(ImDrawList *drawList, const ViewportTransform &transform, ViewportPoint pivotWorld,
                                  float rotationDegrees, SceneTool tool, float uiScale)
        {
            if (drawList == nullptr)
                return;
            const float moveLength = 52.0f * uiScale;
            const float scaleLength = 78.0f * uiScale;
            const float rotateRadius = 102.0f * uiScale;
            const ViewportPoint center = transform.world_to_screen(pivotWorld);
            const ViewportPoint rotatedX = rotate({1.0f, 0.0f}, rotationDegrees);
            const ViewportPoint rotatedY = rotate({0.0f, 1.0f}, rotationDegrees);
            const std::array<ViewportPoint, 2> moveAxes{{{1.0f, 0.0f}, {0.0f, 1.0f}}};
            const std::array<ViewportPoint, 2> scaleAxes{{rotatedX, rotatedY}};
            const std::array<ImU32, 2> axisColors{ImGui::GetColorU32(kAxisXColor), ImGui::GetColorU32(kAxisYColor)};

            if (tool == SceneTool::Rotate)
            {
                drawList->AddCircle({center.x, center.y}, rotateRadius, IM_COL32(245, 190, 75, 235),
                                    72, 2.5f * uiScale);
                const float angle = rotationDegrees * std::numbers::pi_v<float> / 180.0f;
                drawList->AddCircleFilled({center.x + std::cos(angle) * rotateRadius,
                                           center.y + std::sin(angle) * rotateRadius},
                                          6.0f * uiScale, IM_COL32(255, 220, 115, 255));
            }

            if (tool == SceneTool::Scale)
            {
                for (std::size_t index = 0; index < scaleAxes.size(); ++index)
                {
                    const ViewportPoint axis = normalized(scaleAxes[index]);
                    const ViewportPoint start{center.x + axis.x * (moveLength + 6.0f * uiScale),
                                              center.y + axis.y * (moveLength + 6.0f * uiScale)};
                    const ViewportPoint end{center.x + axis.x * scaleLength,
                                            center.y + axis.y * scaleLength};
                    drawList->AddLine({start.x, start.y}, {end.x, end.y}, axisColors[index],
                                      3.0f * uiScale);
                    const float handleSize = 6.0f * uiScale;
                    drawList->AddRectFilled({end.x - handleSize, end.y - handleSize},
                                            {end.x + handleSize, end.y + handleSize},
                                            axisColors[index], 1.5f * uiScale);
                    const char *axisLabel = index == 0 ? "X local" : "Y local";
                    drawList->AddText({end.x + 8.0f * uiScale, end.y + 2.0f * uiScale},
                                      axisColors[index], axisLabel);
                }
            }

            if (tool == SceneTool::Move)
            {
                for (std::size_t index = 0; index < moveAxes.size(); ++index)
                {
                    const ViewportPoint axis = normalized(moveAxes[index]);
                    const ViewportPoint end{center.x + axis.x * moveLength,
                                            center.y + axis.y * moveLength};
                    const ViewportPoint perpendicular{-axis.y, axis.x};
                    drawList->AddLine({center.x, center.y}, {end.x, end.y}, axisColors[index], 3.0f * uiScale);
                    const ImVec2 tip{end.x + axis.x * 7.0f * uiScale, end.y + axis.y * 7.0f * uiScale};
                    drawList->AddTriangleFilled(tip,
                                                {end.x - axis.x * 6.0f * uiScale + perpendicular.x * 5.0f * uiScale,
                                                 end.y - axis.y * 6.0f * uiScale + perpendicular.y * 5.0f * uiScale},
                                                {end.x - axis.x * 6.0f * uiScale - perpendicular.x * 5.0f * uiScale,
                                                 end.y - axis.y * 6.0f * uiScale - perpendicular.y * 5.0f * uiScale},
                                                axisColors[index]);
                    drawList->AddText({end.x + 7.0f * uiScale, end.y + 2.0f * uiScale},
                                      axisColors[index],
                                      index == 0 ? "X world" : "Y world");
                }
            }
            if (tool != SceneTool::Rotate)
                drawList->AddCircleFilled({center.x, center.y}, 4.5f * uiScale, IM_COL32(240, 240, 245, 255));
        }

        struct RectangleHandle
        {
            SceneGestureKind kind = SceneGestureKind::RectangleWidth;
            ViewportPoint screen{};
            ViewportPoint outwardAxis{};
            int componentId = 0;
            std::string property;
            float initialValue = 0.0f;
            float scale = 1.0f;
            float edgeMultiplier = 2.0f;
            ViewportPoint initialOffset{};
            bool preserveOppositeEdge = false;
        };

        std::vector<RectangleHandle> rectangle_handles(const DocumentSession &document,
                                                       const EditorPreview &preview,
                                                       const Selection &selection,
                                                       const ViewportTransform &transform)
        {
            std::vector<RectangleHandle> result;
            if (!selection.objectId)
                return result;
            const auto *info = preview.object_info(*selection.objectId);
            const Json *object = resolve_object(document, *selection.objectId);
            if (info == nullptr || object == nullptr)
                return result;

            int componentType = -1;
            int componentId = -1;
            if (selection.componentId)
            {
                const auto location = document.indexes.componentsById.find(*selection.componentId);
                if (location != document.indexes.componentsById.end() &&
                    location->second.ownerObjectId == *selection.objectId)
                {
                    const auto components = object->find("components");
                    if (components != object->end() && components->is_array() &&
                        location->second.componentIndex < components->size())
                    {
                        const Json &candidate = (*components)[location->second.componentIndex];
                        componentType = candidate.value("type", -1);
                        if (componentType == SPRITE || componentType == COLLIDER)
                            componentId = *selection.componentId;
                    }
                }
            }
            if (componentId < 0)
            {
                if (info->hasSprite)
                {
                    componentType = SPRITE;
                    componentId = info->componentIds[SPRITE];
                }
                else if (info->colliderShape != PreviewColliderShape::None)
                {
                    componentType = COLLIDER;
                    componentId = info->componentIds[COLLIDER];
                }
            }
            const auto location = document.indexes.componentsById.find(componentId);
            const auto components = object->find("components");
            if (componentId < 0 || location == document.indexes.componentsById.end() ||
                components == object->end() || !components->is_array() ||
                location->second.componentIndex >= components->size())
                return result;
            const Json &component = (*components)[location->second.componentIndex];

            ViewportPoint center{info->worldX, info->worldY};
            float width = 0.0f;
            float height = 0.0f;
            std::optional<float> widthValue;
            std::optional<float> heightValue;
            float widthScale = 1.0f;
            float heightScale = 1.0f;
            bool circle = false;
            bool preserveOppositeEdge = false;
            ViewportPoint initialOffset{};
            if (componentType == SPRITE)
            {
                width = std::abs(info->width);
                height = std::abs(info->height);
                widthValue = finite_number_property(component, "width");
                heightValue = finite_number_property(component, "height");
                const auto objectScale = scale_of(*object);
                widthScale = std::max(0.0001f, std::abs(objectScale.x));
                heightScale = std::max(0.0001f, std::abs(objectScale.y));
            }
            else
            {
                circle = info->colliderShape == PreviewColliderShape::Circle;
                const auto offset = rotate({info->colliderOffsetX, info->colliderOffsetY}, info->rotationDegrees);
                center.x += offset.x;
                center.y += offset.y;
                if (circle)
                {
                    width = height = std::abs(info->colliderRadius) * 2.0f;
                    widthValue = finite_number_property(component, "radius");
                    const auto objectScale = scale_of(*object);
                    widthScale = std::max(0.0001f, std::max(std::abs(objectScale.x), std::abs(objectScale.y)));
                }
                else
                {
                    width = std::abs(info->colliderWidth);
                    height = std::abs(info->colliderHeight);
                    widthValue = finite_number_property(component, "width");
                    heightValue = finite_number_property(component, "height");
                    const auto offset = component.find("offset");
                    if (offset == component.end() || !offset->is_object())
                        return result;
                    const auto offsetX = finite_number_property(*offset, "x");
                    const auto offsetY = finite_number_property(*offset, "y");
                    if (!offsetX || !offsetY)
                        return result;
                    initialOffset = {*offsetX, *offsetY};
                    preserveOppositeEdge = true;
                    const auto objectScale = scale_of(*object);
                    widthScale = std::max(0.0001f, std::abs(objectScale.x));
                    heightScale = std::max(0.0001f, std::abs(objectScale.y));
                }
            }

            const ViewportPoint axisX = normalized(rotate({1.0f, 0.0f}, info->rotationDegrees));
            const ViewportPoint axisY = normalized(rotate({0.0f, 1.0f}, info->rotationDegrees));
            const auto add = [&](SceneGestureKind kind, ViewportPoint axis, float halfExtent,
                                 const char *property, float value, float scale,
                                 float edgeMultiplier = 2.0f)
            {
                const ViewportPoint edge{center.x + axis.x * halfExtent, center.y + axis.y * halfExtent};
                result.push_back({kind, transform.world_to_screen(edge), axis, componentId,
                                  property, value, scale, edgeMultiplier, initialOffset,
                                  preserveOppositeEdge});
            };
            if (circle)
            {
                if (widthValue)
                    add(SceneGestureKind::RectangleWidth, axisX, width * 0.5f, "radius", *widthValue,
                        widthScale, 1.0f);
            }
            else
            {
                if (widthValue)
                {
                    add(SceneGestureKind::RectangleWidth, axisX, width * 0.5f, "width", *widthValue, widthScale);
                    add(SceneGestureKind::RectangleWidth, {-axisX.x, -axisX.y}, width * 0.5f,
                        "width", *widthValue, widthScale);
                }
                if (heightValue)
                {
                    add(SceneGestureKind::RectangleHeight, axisY, height * 0.5f, "height", *heightValue, heightScale);
                    add(SceneGestureKind::RectangleHeight, {-axisY.x, -axisY.y}, height * 0.5f,
                        "height", *heightValue, heightScale);
                }
            }
            return result;
        }

        std::optional<RectangleHandle> hit_rectangle_handle(const DocumentSession &document,
                                                            const EditorPreview &preview,
                                                            const Selection &selection,
                                                            const ViewportTransform &transform,
                                                            ViewportPoint mouse,
                                                            float uiScale)
        {
            for (const auto &handle : rectangle_handles(document, preview, selection, transform))
            {
                const float hitRadius = 11.0f * uiScale;
                if (distance_squared(handle.screen, mouse) <= hitRadius * hitRadius)
                    return handle;
            }
            return std::nullopt;
        }

        std::optional<std::pair<int, const Json *>> component_at_type_occurrence(
            const DocumentSession &document, int objectId, int componentType, std::size_t occurrence)
        {
            const auto objectLocation = document.indexes.objectsById.find(objectId);
            if (objectLocation == document.indexes.objectsById.end())
                return std::nullopt;
            const Json *object = resolve_object(document, objectId);
            if (object == nullptr)
                return std::nullopt;
            const auto components = object->find("components");
            if (components == object->end() || !components->is_array())
                return std::nullopt;
            std::size_t currentOccurrence = 0;
            for (const auto &component : *components)
            {
                if (!component.is_object() || component.value("type", -1) != componentType)
                    continue;
                if (currentOccurrence++ != occurrence)
                    continue;
                const auto id = component.find("id");
                if (id == component.end() || !id->is_number_integer())
                    return std::nullopt;
                try
                {
                    const int componentId = id->get<int>();
                    return componentId >= 0 ? std::optional<std::pair<int, const Json *>>(
                                                  std::pair<int, const Json *>{componentId, &component})
                                            : std::nullopt;
                }
                catch (...)
                {
                    return std::nullopt;
                }
            }
            return std::nullopt;
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
                const auto dimensions = interpret_camera_dimensions(*camera);
                result.width = std::max(1.0f, static_cast<float>(dimensions.width.value_or(result.width)));
                result.height = std::max(1.0f, static_cast<float>(dimensions.height.value_or(result.height)));
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
                if (info->bounds.width <= 0.0f || info->bounds.height <= 0.0f)
                    continue;
                minX = std::min(minX, info->bounds.x);
                minY = std::min(minY, info->bounds.y);
                maxX = std::max(maxX, info->bounds.x + info->bounds.width);
                maxY = std::max(maxY, info->bounds.y + info->bounds.height);
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

    float snap_scene_value(float value, float increment) noexcept
    {
        if (!std::isfinite(value) || !std::isfinite(increment) || increment <= 0.0f)
            return value;
        return std::round(value / increment) * increment;
    }

    float resized_rectangle_dimension(float initial, float signedEdgeDeltaWorld,
                                      float absoluteScale, float edgeMultiplier) noexcept
    {
        if (!std::isfinite(initial) || !std::isfinite(signedEdgeDeltaWorld) ||
            !std::isfinite(absoluteScale) || !std::isfinite(edgeMultiplier) ||
            absoluteScale <= 0.0f || edgeMultiplier <= 0.0f)
            return std::max(0.01f, std::isfinite(initial) ? initial : 0.01f);
        const double resized = static_cast<double>(initial) +
                               static_cast<double>(edgeMultiplier) * signedEdgeDeltaWorld / absoluteScale;
        if (!std::isfinite(resized) || resized > std::numeric_limits<float>::max())
            return std::max(0.01f, initial);
        return std::max(0.01f, static_cast<float>(resized));
    }

    ViewportPoint rectangle_offset_after_edge_resize(ViewportPoint initialOffset,
                                                     ViewportPoint outwardAxisWorld,
                                                     float rotationDegrees,
                                                     float initialDimension,
                                                     float resizedDimension,
                                                     float absoluteScale) noexcept
    {
        if (!std::isfinite(initialOffset.x) || !std::isfinite(initialOffset.y) ||
            !std::isfinite(outwardAxisWorld.x) || !std::isfinite(outwardAxisWorld.y) ||
            !std::isfinite(rotationDegrees) || !std::isfinite(initialDimension) ||
            !std::isfinite(resizedDimension) || !std::isfinite(absoluteScale) || absoluteScale <= 0.0f)
            return initialOffset;
        const float centerShift = (resizedDimension - initialDimension) * absoluteScale * 0.5f;
        const ViewportPoint localShift = rotate({outwardAxisWorld.x * centerShift,
                                                 outwardAxisWorld.y * centerShift},
                                                -rotationDegrees);
        const ViewportPoint result{initialOffset.x + localShift.x, initialOffset.y + localShift.y};
        return std::isfinite(result.x) && std::isfinite(result.y) ? result : initialOffset;
    }

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
        previousTargets_.clear();
        if (repeated)
            index_ = (index_ + 1) % candidates.size();
        else
            index_ = 0;
        previousCandidates_ = candidates;
        previousPoint_ = screenPoint;
        hasPrevious_ = true;
        return candidates[index_];
    }

    std::optional<ViewportPickCycle::Target> ViewportPickCycle::choose(
        const std::vector<Target> &candidates, ViewportPoint screenPoint)
    {
        if (candidates.empty())
        {
            reset();
            return std::nullopt;
        }
        const bool repeated = hasPrevious_ && candidates == previousTargets_ &&
                              distance_squared(screenPoint, previousPoint_) <= 49.0f;
        if (repeated)
            index_ = (index_ + 1) % candidates.size();
        else
            index_ = 0;
        previousCandidates_.clear();
        previousTargets_ = candidates;
        previousPoint_ = screenPoint;
        hasPrevious_ = true;
        return candidates[index_];
    }

    void ViewportPickCycle::reset() noexcept
    {
        previousCandidates_.clear();
        previousTargets_.clear();
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
        update_projection();
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

    bool object_intersects_screen_rect(const ViewportTransform &transform, const PreviewObjectInfo &info,
                                       float minX, float maxX, float minY, float maxY)
    {
        float halfW = 8.0f;
        float halfH = 8.0f;
        if (info.hasSprite)
        {
            halfW = std::max(halfW, std::abs(info.width) * 0.5f);
            halfH = std::max(halfH, std::abs(info.height) * 0.5f);
        }
        if (info.colliderShape != PreviewColliderShape::None)
        {
            const float cW = info.colliderShape == PreviewColliderShape::Circle ? info.colliderRadius * 2.0f : info.colliderWidth;
            const float cH = info.colliderShape == PreviewColliderShape::Circle ? info.colliderRadius * 2.0f : info.colliderHeight;
            halfW = std::max(halfW, cW * 0.5f);
            halfH = std::max(halfH, cH * 0.5f);
        }
        if (info.hasCamera)
        {
            halfW = std::max(halfW, info.cameraWidth * 0.5f);
            halfH = std::max(halfH, info.cameraHeight * 0.5f);
        }
        const auto pCenter = transform.world_to_screen({info.worldX, info.worldY});
        const float scaleX = transform.world_width() > 0.0f ? transform.content_rect().width / transform.world_width() : 1.0f;
        const float scaleY = transform.world_height() > 0.0f ? transform.content_rect().height / transform.world_height() : 1.0f;
        const float screenHalfW = halfW * scaleX;
        const float screenHalfH = halfH * scaleY;
        const float objMinX = pCenter.x - screenHalfW;
        const float objMaxX = pCenter.x + screenHalfW;
        const float objMinY = pCenter.y - screenHalfH;
        const float objMaxY = pCenter.y + screenHalfH;

        return !(objMaxX < minX || objMinX > maxX || objMaxY < minY || objMinY > maxY);
    }

    std::vector<int> viewport_pick_candidates(const ViewportTransform &transform,
                                              const std::vector<int> &orderedDocumentIds,
                                              const EditorPreview &preview, ViewportPoint screenPoint,
                                              float tolerancePixels)
    {
        const auto targets = viewport_pick_targets(transform, orderedDocumentIds, preview,
                                                   screenPoint, tolerancePixels);
        std::vector<int> result;
        result.reserve(targets.size());
        for (const auto &target : targets)
            if (result.empty() || result.back() != target.objectId)
                result.push_back(target.objectId);
        return result;
    }

    std::vector<int> viewport_pickable_ids(const std::vector<int> &orderedDocumentIds,
                                           const std::vector<int> &hiddenObjectIds,
                                           const std::vector<int> &lockedObjectIds)
    {
        std::vector<int> result;
        result.reserve(orderedDocumentIds.size());
        for (const int objectId : orderedDocumentIds)
            if (!std::binary_search(hiddenObjectIds.begin(), hiddenObjectIds.end(), objectId) &&
                !std::binary_search(lockedObjectIds.begin(), lockedObjectIds.end(), objectId))
                result.push_back(objectId);
        return result;
    }

    std::vector<ViewportPickCycle::Target> viewport_pick_targets(
        const ViewportTransform &transform, const std::vector<int> &orderedDocumentIds,
        const EditorPreview &preview, ViewportPoint screenPoint, float tolerancePixels)
    {
        const auto worldPoint = transform.screen_to_world(screenPoint);
        if (!worldPoint || !std::isfinite(tolerancePixels) || tolerancePixels < 0.0f)
            return {};
        const float toleranceWorld = tolerancePixels * std::max(
                                                           transform.world_width() / transform.content_rect().width,
                                                           transform.world_height() / transform.content_rect().height);
        std::vector<ViewportPickCycle::Target> result;
        for (auto iter = orderedDocumentIds.rbegin(); iter != orderedDocumentIds.rend(); ++iter)
        {
            const PreviewObjectInfo *info = preview.object_info(*iter);
            if (info == nullptr)
                continue;
            const ViewportPoint objectCenter{info->worldX, info->worldY};
            const float objectRotation = info->rotationDegrees;
            ViewportPoint local = rotate({worldPoint->x - objectCenter.x, worldPoint->y - objectCenter.y}, -objectRotation);
            bool colliderHit = false;
            if (info->colliderShape != PreviewColliderShape::None)
            {
                const auto offset = rotate({info->colliderOffsetX, info->colliderOffsetY}, objectRotation);
                const ViewportPoint colliderCenter{objectCenter.x + offset.x, objectCenter.y + offset.y};
                local = rotate({worldPoint->x - colliderCenter.x, worldPoint->y - colliderCenter.y}, -objectRotation);
                if (info->colliderShape == PreviewColliderShape::Circle)
                {
                    const float radius = info->colliderRadius + toleranceWorld;
                    colliderHit = local.x * local.x + local.y * local.y <= radius * radius;
                }
                else
                    colliderHit = std::abs(local.x) <= info->colliderWidth * 0.5f + toleranceWorld &&
                                  std::abs(local.y) <= info->colliderHeight * 0.5f + toleranceWorld;
            }
            if (colliderHit)
                result.push_back({*iter, info->componentIds[COLLIDER]});

            bool cameraHit = false;
            if (info->hasCamera)
            {
                const float dx = std::abs(worldPoint->x - (info->worldX + info->cameraWidth * 0.5f));
                const float dy = std::abs(worldPoint->y - (info->worldY + info->cameraHeight * 0.5f));
                cameraHit = (dx <= info->cameraWidth * 0.5f + toleranceWorld &&
                             std::abs(dy - info->cameraHeight * 0.5f) <= toleranceWorld) ||
                            (dy <= info->cameraHeight * 0.5f + toleranceWorld &&
                             std::abs(dx - info->cameraWidth * 0.5f) <= toleranceWorld);
            }
            if (cameraHit)
                result.push_back({*iter, info->componentIds[CAMERA]});

            const ViewportPoint spriteLocal = rotate(
                {worldPoint->x - objectCenter.x, worldPoint->y - objectCenter.y}, -objectRotation);
            const bool spriteHit = info->hasSprite &&
                                   std::abs(spriteLocal.x) <= std::abs(info->width) * 0.5f + toleranceWorld &&
                                   std::abs(spriteLocal.y) <= std::abs(info->height) * 0.5f + toleranceWorld;
            if (spriteHit)
                result.push_back({*iter, info->componentIds[SPRITE]});

            if (result.empty() || result.back().objectId != *iter)
            {
                if (info->emptyMarker && std::abs(spriteLocal.x) <= 6.0f + toleranceWorld &&
                    std::abs(spriteLocal.y) <= 6.0f + toleranceWorld)
                    result.push_back({*iter, std::nullopt});
            }
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
                                                                 DocumentSession &document, int objectId,
                                                                 SelectionState selection)
    {
        if (resolve_object(document, objectId) == nullptr)
            return std::nullopt;
        PropertyAddress address = address_for_document(document, objectId, std::nullopt, std::nullopt,
                                                       {std::string("position")});
        return commands.begin_gesture(document, address, selection);
    }

    std::optional<PropertyGesture> begin_collider_offset_gesture(EditCommandService &commands,
                                                                 DocumentSession &document, int objectId,
                                                                 SelectionState selection)
    {
        const Json *object = resolve_object(document, objectId);
        const Json *collider = object == nullptr ? nullptr : component_of_type(*object, 3);
        if (collider == nullptr)
            return std::nullopt;
        const auto componentId = collider->value("id", -1);
        if (componentId < 0)
            return std::nullopt;
        PropertyAddress address = address_for_document(document, objectId, componentId, std::nullopt,
                                                       {std::string("offset")});
        return commands.begin_gesture(document, address, selection);
    }

    void SceneViewportPanel::cancel_gesture(EditCommandService &commands, DocumentSession &document)
    {
        if (gesture_ && gesture_->active())
            gesture_->cancel(commands, document);
        if (batchGesture_ && batchGesture_->active())
            batchGesture_->cancel(commands, document);
        gesture_.reset();
        batchGesture_.reset();
        gestureObjectIds_.clear();
        gestureStartWorldPositions_.clear();
        gestureStartScales_.clear();
        gestureStartRotations_.clear();
        groupTransformGesture_ = false;
        gestureRectangleStartSizes_.clear();
        gestureRectangleScales_.clear();
        gestureRectangleRotations_.clear();
        gestureRectangleOffsets_.clear();
        gestureRectangleAxes_.clear();
        groupRectangleGesture_ = false;
        pendingDrag_ = false;
        panning_ = false;
        marquee_.active = false;
    }

    void SceneViewportPanel::commit_gesture()
    {
        marquee_.active = false;
        if (gesture_)
        {
            gesture_->commit();
            gesture_.reset();
        }
        if (batchGesture_)
        {
            batchGesture_->commit();
            batchGesture_.reset();
        }
        gestureObjectIds_.clear();
        gestureStartWorldPositions_.clear();
        gestureStartScales_.clear();
        gestureStartRotations_.clear();
        groupTransformGesture_ = false;
        gestureRectangleStartSizes_.clear();
        gestureRectangleScales_.clear();
        gestureRectangleRotations_.clear();
        gestureRectangleOffsets_.clear();
        gestureRectangleAxes_.clear();
        groupRectangleGesture_ = false;
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
    void SceneViewportPanel::set_tool(SceneTool tool) noexcept { tool_ = tool; }
    SceneTool SceneViewportPanel::tool() const noexcept { return tool_; }
    void SceneViewportPanel::reset_view_state(const DocumentSession &document) noexcept
    {
        viewDocumentGeneration_ = document.documentGeneration;
        viewProjectGeneration_ = document.projectGeneration;
        viewTabId_ = document.tabId;
        hasView_ = false;
    }

    void SceneViewportPanel::restore_view_state(const DocumentSession &document, float centerX,
                                                float centerY, float zoom) noexcept
    {
        viewDocumentGeneration_ = document.documentGeneration;
        viewProjectGeneration_ = document.projectGeneration;
        viewTabId_ = document.tabId;
        if (!std::isfinite(centerX) || !std::isfinite(centerY) || !std::isfinite(zoom) || zoom <= 0.0f)
        {
            hasView_ = false;
            return;
        }
        viewCenter_ = {centerX, centerY};
        viewZoom_ = std::clamp(zoom, 0.05f, 64.0f);
        hasView_ = true;
    }

    SceneViewportPanel::ViewState SceneViewportPanel::view_state() const noexcept
    {
        return {viewCenter_, viewZoom_};
    }

    bool SceneViewportPanel::view_matches_document(const DocumentSession &document) const noexcept
    {
        return viewDocumentGeneration_ == document.documentGeneration &&
               viewProjectGeneration_ == document.projectGeneration && viewTabId_ == document.tabId;
    }

    bool SceneViewportPanel::gesture_active() const noexcept
    {
        return (gesture_ && gesture_->active()) || (batchGesture_ && batchGesture_->active());
    }

    bool SceneViewportPanel::has_active_input() const noexcept
    {
        return gesture_active() || pendingDrag_ || panning_ || marquee_.active;
    }

    void SceneViewportPanel::draw(DocumentSession &document, EditorPreview &preview, SelectionModel &selection,
                                  EditCommandService &commands, SDL_Window *window, SDL_Renderer *renderer,
                                  SnapPreferences &snapPreferences, bool modalOrTextCapture,
                                  bool editingEnabled, std::vector<int> *hiddenObjectIds,
                                  std::vector<int> *lockedObjectIds,
                                  std::function<SDL_Texture *(SDL_Texture *, int, int)> imageResolver)
    {
        if (!open_)
        {
            cancel_gesture(commands, document);
            return;
        }
        if (focusPending_)
        {
            ImGui::SetNextWindowFocus();
            focusPending_ = false;
        }
        if (!ImGui::Begin("Scene###panel.scene", &open_,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar))
        {
            cancel_gesture(commands, document);
            ImGui::End();
            return;
        }
        const std::unordered_set<int> hiddenIds = hiddenObjectIds == nullptr
                                                      ? std::unordered_set<int>{}
                                                      : std::unordered_set<int>(hiddenObjectIds->begin(), hiddenObjectIds->end());
        const std::unordered_set<int> lockedIds = lockedObjectIds == nullptr
                                                      ? std::unordered_set<int>{}
                                                      : std::unordered_set<int>(lockedObjectIds->begin(), lockedObjectIds->end());
        const auto gesture_roots = [&](const Selection &current)
        {
            auto roots = selected_top_level_roots(document, current);
            std::erase_if(roots, [&lockedIds](int id)
                          { return lockedIds.contains(id); });
            return roots;
        };
        const CameraDefaults camera = document_camera(document);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float uiScale = std::clamp(ImGui::GetIO().FontGlobalScale, 0.5f, 6.0f);
        if (!view_matches_document(document))
            reset_view_state(document);
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

        const float toolbarHeight = ImGui::GetFrameHeight();
        const ImVec2 toolBtnSize(toolbarHeight, toolbarHeight);

        ImGui::BeginDisabled(has_active_input());
        if (icon_button(EditorIcon::HandTool, "##tool-hand", tool_ == SceneTool::Hand,
                        "Hand Tool (Q) - Pan the scene view with left-click drag", toolBtnSize))
            tool_ = SceneTool::Hand;
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button(EditorIcon::MoveTool, "##tool-move", tool_ == SceneTool::Move,
                        "Move Tool (W) - Select and drag objects", toolBtnSize))
            tool_ = SceneTool::Move;
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button(EditorIcon::RotateTool, "##tool-rotate", tool_ == SceneTool::Rotate,
                        "Rotate Tool (E) - Rotate the selected object", toolBtnSize))
            tool_ = SceneTool::Rotate;
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button(EditorIcon::ScaleTool, "##tool-scale", tool_ == SceneTool::Scale,
                        "Scale Tool (R) - Scale the selected object", toolBtnSize))
            tool_ = SceneTool::Scale;
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button(EditorIcon::RectTool, "##tool-rect", tool_ == SceneTool::Rectangle,
                        "Rectangle Tool (T) - Resize sprite and collider bounds", toolBtnSize))
            tool_ = SceneTool::Rectangle;
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::EndDisabled();

        const float rightControlsWidth = 320.0f * uiScale;
        const float currentX = ImGui::GetCursorPosX();
        const float availWidth = ImGui::GetContentRegionAvail().x + currentX;
        const bool compactRightControls =
            availWidth <= currentX + rightControlsWidth + 10.0f * uiScale;
        if (!compactRightControls)
            ImGui::SameLine(availWidth - rightControlsWidth);
        else
        {
            const float compactButtonWidth = ImGui::CalcTextSize("View...").x +
                ImGui::GetStyle().FramePadding.x * 2.0f;
            if (availWidth < currentX + compactButtonWidth + ImGui::GetStyle().ItemSpacing.x)
                ImGui::NewLine();
            else
                ImGui::SameLine();
        }
        bool frameSelected = frameSelectedRequested_;
        bool frameAll = frameAllRequested_;
        const auto drawSnapSettings = [&] {
            if (ImGui::BeginPopup("##scene-snap-settings"))
            {
                ImGui::DragFloat("Position step", &snapPreferences.position, 0.1f, 0.0001f, 1000000.0f, "%.2f");
                ImGui::DragFloat("Rotation step (degrees)", &snapPreferences.rotationDegrees, 0.1f, 0.001f, 360.0f, "%.2f");
                ImGui::DragFloat("Scale step", &snapPreferences.scale, 0.01f, 0.0001f, 10.0f, "%.2f");
                const char *coordinateModes[] = {"World", "Local"};
                int coordinateMode = snapPreferences.coordinateMode == "local" ? 1 : 0;
                if (ImGui::Combo("Position coordinates", &coordinateMode, coordinateModes, 2))
                    snapPreferences.coordinateMode = coordinateMode == 1 ? "local" : "world";
                ImGui::TextDisabled("Hold Ctrl while dragging to apply the configured increments.");
                ImGui::EndPopup();
            }
        };
        bool openCompactSnapSettings = false;
        if (compactRightControls)
        {
            if (ImGui::SmallButton("View..."))
                ImGui::OpenPopup("##scene-view-controls");
            if (ImGui::BeginPopup("##scene-view-controls"))
            {
                if (ImGui::MenuItem("Grid", nullptr, showGrid_))
                    showGrid_ = !showGrid_;
                if (ImGui::MenuItem("Snap settings..."))
                    openCompactSnapSettings = true;
                char zoomLabel[48];
                std::snprintf(zoomLabel, sizeof(zoomLabel), "Zoom: %d%% (reset)",
                              static_cast<int>(std::round(viewZoom_ * 100.0f)));
                if (ImGui::MenuItem(zoomLabel))
                    viewZoom_ = 1.0f;
                if (ImGui::MenuItem("Frame selected (F)"))
                    frameSelected = true;
                if (ImGui::MenuItem("Frame all (Shift+F)"))
                    frameAll = true;
                ImGui::EndPopup();
            }
        }
        else
        {
            if (ImGui::SmallButton(showGrid_ ? "Grid" : "Grid (off)"))
                showGrid_ = !showGrid_;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Toggle background grid display");

            ImGui::SameLine();
            char snapBuf[32];
            std::snprintf(snapBuf, sizeof(snapBuf), "Snap: %.2g", snapPreferences.position);
            if (ImGui::SmallButton(snapBuf))
                ImGui::OpenPopup("##scene-snap-settings");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Configure position, rotation, and scale increments; hold Ctrl while dragging to snap");

            ImGui::SameLine();
            char zoomBuf[32];
            std::snprintf(zoomBuf, sizeof(zoomBuf), "%d%%", static_cast<int>(std::round(viewZoom_ * 100.0f)));
            if (ImGui::SmallButton(zoomBuf))
                viewZoom_ = 1.0f;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Viewport zoom level. Click to reset to 100%%.");

            ImGui::SameLine();
            frameSelected = ImGui::SmallButton("Frame") || frameSelected;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frame selected object (F)");
            ImGui::SameLine();
            frameAll = ImGui::SmallButton("All") || frameAll;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frame entire scene (Shift+F)");
        }
        if (openCompactSnapSettings)
            ImGui::OpenPopup("##scene-snap-settings");
        drawSnapSettings();

        frameAllRequested_ = false;
        frameSelectedRequested_ = false;
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
                                      ? static_cast<float>(outputWidth) / windowWidth
                                      : 1.0f;
        const float pixelRatioY = haveOutput && haveWindow && windowHeight > 0
                                      ? static_cast<float>(outputHeight) / windowHeight
                                      : 1.0f;
        const bool transformReady = transform_.configure(imageRect, pixelRatioX, pixelRatioY,
                                                         camera.width, camera.height,
                                                         viewCenter_.x, viewCenter_.y, viewZoom_);
        if (frameAll || (frameSelected && !selection.current().objectId))
        {
            ViewportRect bounds = bounds_for_ids(preview.ordered_authoring_ids(), preview);
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
            const std::vector<int> selected = selected_top_level_roots(document, selection.current());
            ViewportRect bounds = bounds_for_ids(selected, preview);
            if (bounds.width <= 0.0f || bounds.height <= 0.0f)
            {
                const ViewportPoint pos = object_world_position(document, *selection.current().objectId);
                bounds = {pos.x - 50.0f, pos.y - 50.0f, 100.0f, 100.0f};
            }
            if (transformReady && bounds.width > 0.0f && bounds.height > 0.0f && transform_.frame_bounds(bounds))
            {
                viewZoom_ = transform_.zoom();
                viewCenter_ = {transform_.camera_left() + transform_.world_width() * 0.5f,
                               transform_.camera_top() + transform_.world_height() * 0.5f};
            }
        }

        const ViewportRect content = transform_.content_rect();
        ImGui::SetCursorScreenPos({content.x, content.y});
        const bool sceneRendered = transformReady && preview.render(transform_.camera_left(), transform_.camera_top(),
                                                                    transform_.world_width(), transform_.world_height(),
                                                                    transform_.target_width(), transform_.target_height(),
                                                                    &hiddenIds);
        if (sceneRendered && preview.target() != nullptr)
        {
            SDL_Texture *image = imageResolver
                                     ? imageResolver(preview.target(), transform_.target_width(), transform_.target_height())
                                     : preview.target();
            if (image != nullptr)
            {
                const auto textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(image));
                ImGui::Image(ImTextureRef(textureId), ImVec2(content.width, content.height));
            }
            else
                ImGui::Dummy(ImVec2(std::max(content.width, 1.0f), std::max(content.height, 1.0f)));
        }
        else
            ImGui::Dummy(ImVec2(std::max(content.width, 1.0f), std::max(content.height, 1.0f)));
        bool imageHovered = false;
        if (transformReady && content.width > 0.0f && content.height > 0.0f)
        {
            ImGui::SetCursorScreenPos({content.x, content.y});
            ImGui::InvisibleButton("##scene-viewport-input", {content.width, content.height});
            imageHovered = ImGui::IsItemHovered();
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PLATFORMATOR_ASSET_PATH"))
                {
                    std::string assetPath;
                    EditorAssetKind kind = EditorAssetKind::Other;
                    if (payload->DataSize == sizeof(AssetDragPayload))
                    {
                        const auto *data = static_cast<const AssetDragPayload *>(payload->Data);
                        assetPath = data->relativeAssetPath;
                        kind = data->kind;
                    }
                    else if (payload->Data != nullptr && payload->DataSize > 0)
                    {
                        assetPath = std::string(static_cast<const char *>(payload->Data), payload->DataSize);
                        while (!assetPath.empty() && assetPath.back() == '\0')
                            assetPath.pop_back();
                        kind = editor_asset_format(assetPath).kind;
                    }
                    if (!assetPath.empty())
                    {
                        const auto mousePos = ImGui::GetMousePos();
                        const auto worldPos = transform_.screen_to_world({mousePos.x, mousePos.y});
                        const float targetX = worldPos ? worldPos->x : 0.0f;
                        const float targetY = worldPos ? worldPos->y : 0.0f;
                        instantiate_asset_drop(document, commands, selection, assetPath, kind, targetX, targetY, std::nullopt);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect({content.x, content.y}, {content.x + content.width, content.y + content.height}, true);

        if (transformReady && showGrid_)
        {
            constexpr float gridSpacing = 100.0f;
            const float firstX = std::floor(transform_.camera_left() / gridSpacing) * gridSpacing;
            const float firstY = std::floor(transform_.camera_top() / gridSpacing) * gridSpacing;
            for (float x = firstX; x <= transform_.camera_left() + transform_.world_width() &&
                                   x - firstX < gridSpacing * 128.0f;
                 x += gridSpacing)
            {
                const auto point = transform_.world_to_screen({x, transform_.camera_top()});
                drawList->AddLine({point.x, content.y}, {point.x, content.y + content.height},
                                  x == 0.0f ? IM_COL32(90, 112, 125, 140) : IM_COL32(70, 75, 82, 75), 1.0f);
            }
            for (float y = firstY; y <= transform_.camera_top() + transform_.world_height() &&
                                   y - firstY < gridSpacing * 128.0f;
                 y += gridSpacing)
            {
                const auto point = transform_.world_to_screen({transform_.camera_left(), y});
                drawList->AddLine({content.x, point.y}, {content.x + content.width, point.y},
                                  y == 0.0f ? IM_COL32(90, 112, 125, 140) : IM_COL32(70, 75, 82, 75), 1.0f);
            }
            drawList->AddRect({content.x, content.y}, {content.x + content.width, content.y + content.height},
                              IM_COL32(100, 105, 112, 220));
            draw_world_box(drawList, transform_, {camera.x + camera.width * 0.5f, camera.y + camera.height * 0.5f},
                           camera.width, camera.height, 0.0f, IM_COL32(115, 190, 130, 210), 1.5f);
            const auto selectedIds = selection.current().selectedObjectIds;
            const auto selectedId = selection.current().objectId;
            for (const int id : preview.ordered_authoring_ids())
            {
                if (hiddenIds.contains(id))
                    continue;
                const PreviewObjectInfo *info = preview.object_info(id);
                if (info == nullptr)
                    continue;
                const bool selected = (selectedId && *selectedId == id) ||
                                      std::find(selectedIds.begin(), selectedIds.end(), id) != selectedIds.end();
                const ImU32 spriteColor = selected                                     ? IM_COL32(255, 210, 75, 255)
                                          : info->hasSprite && !info->textureAvailable ? IM_COL32(240, 105, 90, 200)
                                          : !info->active                              ? IM_COL32(135, 145, 155, 140)
                                                                                       : IM_COL32(110, 185, 210, 155);
                if (info->hasSprite)
                    draw_world_box(drawList, transform_, {info->worldX, info->worldY}, info->width, info->height,
                                   info->rotationDegrees, spriteColor, selected ? 2.0f : 1.0f);
                if (info->hasCamera)
                {
                    const auto marker = transform_.world_to_screen({info->worldX, info->worldY});
                    const ImU32 cameraColor = selected ? IM_COL32(255, 210, 75, 255)
                                                       : IM_COL32(115, 190, 130, 230);
                    drawList->AddRect({marker.x - 5.0f, marker.y - 5.0f},
                                      {marker.x + 5.0f, marker.y + 5.0f}, cameraColor,
                                      1.0f, 0, selected ? 2.0f : 1.0f);
                    drawList->AddLine({marker.x - 3.0f, marker.y}, {marker.x + 3.0f, marker.y}, cameraColor);
                    drawList->AddLine({marker.x, marker.y - 3.0f}, {marker.x, marker.y + 3.0f}, cameraColor);
                }
                if (info->emptyMarker)
                {
                    const auto marker = transform_.world_to_screen({info->worldX, info->worldY});
                    drawList->AddRect({marker.x - 5.0f, marker.y - 5.0f}, {marker.x + 5.0f, marker.y + 5.0f},
                                      selected ? IM_COL32(255, 210, 75, 255) : IM_COL32(155, 170, 185, 210),
                                      0.0f, 0, selected ? 2.0f : 1.0f);
                    drawList->AddLine({marker.x - 3.0f, marker.y}, {marker.x + 3.0f, marker.y},
                                      IM_COL32(155, 170, 185, 210));
                    drawList->AddLine({marker.x, marker.y - 3.0f}, {marker.x, marker.y + 3.0f},
                                      IM_COL32(155, 170, 185, 210));
                }
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
        if (transformReady && selection.current().objectId &&
            !hiddenIds.contains(*selection.current().objectId) &&
            !lockedIds.contains(*selection.current().objectId))
        {
            const Selection selected = selection.current();
            if (const Json *selectedObject = resolve_object(document, *selected.objectId))
            {
                if (tool_ == SceneTool::Rectangle)
                {
                    for (const auto &handle : rectangle_handles(document, preview, selected, transform_))
                        drawList->AddRectFilled({handle.screen.x - 5.0f * uiScale,
                                                 handle.screen.y - 5.0f * uiScale},
                                                {handle.screen.x + 5.0f * uiScale,
                                                 handle.screen.y + 5.0f * uiScale},
                                                IM_COL32(250, 170, 80, 255), 1.0f * uiScale);
                }
                else if (tool_ != SceneTool::Hand)
                {
                    const float rotationDegrees = rotation_of(*selectedObject) * 180.0f /
                                                  std::numbers::pi_v<float>;
                    draw_transform_gizmo(drawList, transform_,
                                         object_world_position(document, *selected.objectId),
                                         rotationDegrees, tool_, uiScale);
                }
            }
        }
        if (transformReady && content.width > 16.0f && content.height > 16.0f)
        {
            const float overlayPad = 8.0f * uiScale;
            if (tool_ == SceneTool::Rotate && selection.current().objectId)
            {
                if (const Json *rotating = resolve_object(document, *selection.current().objectId))
                {
                    char angleText[48];
                    std::snprintf(angleText, sizeof(angleText), "Angle %.1f deg",
                                  rotation_of(*rotating) * 180.0f / std::numbers::pi_v<float>);
                    const ImVec2 angleSize = ImGui::CalcTextSize(angleText);
                    const ImVec2 angleMinimum{content.x + overlayPad,
                                              content.y + content.height - angleSize.y - overlayPad * 2.0f};
                    drawList->AddRectFilled(angleMinimum,
                                            {angleMinimum.x + angleSize.x + overlayPad * 2.0f,
                                             angleMinimum.y + angleSize.y + overlayPad},
                                            IM_COL32(18, 20, 24, 190), 3.0f);
                    drawList->AddText({angleMinimum.x + overlayPad, angleMinimum.y + overlayPad * 0.5f},
                                      IM_COL32(190, 196, 205, 235), angleText);
                }
            }
        }
        if (marquee_.active)
        {
            const float minX = std::min(marquee_.startScreen.x, marquee_.currentScreen.x);
            const float maxX = std::max(marquee_.startScreen.x, marquee_.currentScreen.x);
            const float minY = std::min(marquee_.startScreen.y, marquee_.currentScreen.y);
            const float maxY = std::max(marquee_.startScreen.y, marquee_.currentScreen.y);
            drawList->AddRectFilled({minX, minY}, {maxX, maxY}, IM_COL32(50, 130, 220, 50));
            drawList->AddRect({minX, minY}, {maxX, maxY}, IM_COL32(70, 160, 255, 220), 0.0f, 0, 1.0f);
        }
        drawList->PopClipRect();

        ImGuiIO &io = ImGui::GetIO();
        const ViewportPoint mouse{io.MousePos.x, io.MousePos.y};
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool pressedOnScene = imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool capture = modalOrTextCapture || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
                             (io.WantTextInput && !pressedOnScene);
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool appHasFocus = window == nullptr || (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        const bool sceneFocused = focused || selection.current().focusedPanel == FocusedPanel::Scene;
        const bool hierarchyFocused = selection.current().focusedPanel == FocusedPanel::Hierarchy;
        const EditorInputCapture sceneCapture{appHasFocus, capture, false, has_active_input(),
                                              sceneFocused, hierarchyFocused, appHasFocus};
        if (ImGui::IsKeyPressed(ImGuiKey_F))
        {
            const auto frameAction = dispatch_editor_key(EditorKey::F, io.KeyShift, io.KeyCtrl, sceneCapture);
            if (frameAction == EditorAction::FrameSelected)
                frameSelectedRequested_ = true;
            else if (frameAction == EditorAction::FrameAll)
                frameAllRequested_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Q))
            if (dispatch_editor_key(EditorKey::Q, io.KeyShift, io.KeyCtrl, sceneCapture) ==
                EditorAction::SelectHandTool)
                tool_ = SceneTool::Hand;
        if (ImGui::IsKeyPressed(ImGuiKey_W))
            if (dispatch_editor_key(EditorKey::W, io.KeyShift, io.KeyCtrl, sceneCapture) ==
                EditorAction::SelectMoveTool)
                tool_ = SceneTool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E))
            if (dispatch_editor_key(EditorKey::E, io.KeyShift, io.KeyCtrl, sceneCapture) ==
                EditorAction::SelectRotateTool)
                tool_ = SceneTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R))
            if (dispatch_editor_key(EditorKey::R, io.KeyShift, io.KeyCtrl, sceneCapture) ==
                EditorAction::SelectScaleTool)
                tool_ = SceneTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_T))
            if (dispatch_editor_key(EditorKey::T, io.KeyShift, io.KeyCtrl, sceneCapture) ==
                EditorAction::SelectRectangleTool)
                tool_ = SceneTool::Rectangle;
        if (!editingEnabled)
        {
            cancel_gesture(commands, document);
            pendingDrag_ = false;
        }
        if (!transformReady || !appHasFocus)
            cancel_gesture(commands, document);
        if ((gesture_active() && (capture || !appHasFocus || !selection.current().objectId ||
                                  gestureAddress_.documentGeneration != document.documentGeneration ||
                                  gestureAddress_.identity != DocumentIdentity{
                                                                  ProjectGeneration{document.projectGeneration}, document.tabId,
                                                                  DocumentGeneration{document.documentGeneration}} ||
                                  *selection.current().objectId != gestureAddress_.objectId || escape || (groupTransformGesture_ && gesture_roots(selection.current()) != gestureObjectIds_) || (groupRectangleGesture_ && gesture_roots(selection.current()) != gestureObjectIds_))) ||
            (pendingDrag_ && (capture || !appHasFocus || escape || !selection.current().objectId || *selection.current().objectId != pendingDragObjectId_)))
            cancel_gesture(commands, document);

        if (capture || !appHasFocus || escape || !editingEnabled)
            marquee_.active = false;

        if (marquee_.active)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float minX = std::min(marquee_.startScreen.x, marquee_.currentScreen.x);
                const float maxX = std::max(marquee_.startScreen.x, marquee_.currentScreen.x);
                const float minY = std::min(marquee_.startScreen.y, marquee_.currentScreen.y);
                const float maxY = std::max(marquee_.startScreen.y, marquee_.currentScreen.y);
                const float dragDistSq = (maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY);
                if (dragDistSq >= 16.0f)
                {
                    static const std::vector<int> noVisibilityOverrides;
                    const auto &hiddenList = hiddenObjectIds == nullptr ? noVisibilityOverrides : *hiddenObjectIds;
                    const auto &lockedList = lockedObjectIds == nullptr ? noVisibilityOverrides : *lockedObjectIds;
                    const auto pickableIds = viewport_pickable_ids(preview.ordered_authoring_ids(), hiddenList, lockedList);

                    std::vector<int> enclosedIds;
                    for (const int id : pickableIds)
                    {
                        const PreviewObjectInfo *info = preview.object_info(id);
                        if (info != nullptr && object_intersects_screen_rect(transform_, *info, minX, maxX, minY, maxY))
                            enclosedIds.push_back(id);
                    }

                    Selection sel = selection.current();
                    sel.focusedPanel = FocusedPanel::Scene;
                    if (marquee_.additive)
                    {
                        for (const int id : enclosedIds)
                        {
                            if (std::find(sel.selectedObjectIds.begin(), sel.selectedObjectIds.end(), id) == sel.selectedObjectIds.end())
                                sel.selectedObjectIds.push_back(id);
                        }
                    }
                    else
                    {
                        sel.selectedObjectIds = enclosedIds;
                    }
                    if (!sel.selectedObjectIds.empty())
                    {
                        sel.objectId = sel.selectedObjectIds.front();
                        sel.rangeAnchorObjectId = sel.selectedObjectIds.front();
                    }
                    else if (!marquee_.additive)
                    {
                        sel.objectId.reset();
                        sel.rangeAnchorObjectId.reset();
                    }
                    sel.componentId.reset();
                    sel.behaviorOrdinal.reset();
                    selection.set(document, sel);
                }
                marquee_.active = false;
            }
            else
            {
                marquee_.currentScreen = mouse;
            }
        }

        const auto world_at_screen = [&](ViewportPoint point)
        {
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
                gestureObjectIds_.clear();
                gestureStartWorldPositions_.clear();
                gestureStartScales_.clear();
                gestureStartRotations_.clear();
                groupTransformGesture_ = false;
                gestureRectangleStartSizes_.clear();
                gestureRectangleScales_.clear();
                gestureRectangleRotations_.clear();
                gestureRectangleOffsets_.clear();
                gestureRectangleAxes_.clear();
                groupRectangleGesture_ = false;
                switch (pendingGestureKind_)
                {
                case SceneGestureKind::Position:
                case SceneGestureKind::Scale:
                case SceneGestureKind::Rotation:
                {
                    const auto roots = gesture_roots(currentSelection);
                    bool canGroup = roots.size() > 1;
                    std::vector<PropertyAddress> addresses;
                    std::vector<ViewportPoint> worldPositions;
                    std::vector<ViewportPoint> scales;
                    std::vector<float> rotations;
                    if (canGroup)
                    {
                        addresses.reserve(roots.size());
                        worldPositions.reserve(roots.size());
                        scales.reserve(roots.size());
                        rotations.reserve(roots.size());
                        for (const int objectId : roots)
                        {
                            const Json *object = resolve_object(document, objectId);
                            if (object == nullptr)
                            {
                                canGroup = false;
                                break;
                            }
                            const auto position = object_world_position(document, objectId);
                            if (!std::isfinite(position.x) || !std::isfinite(position.y))
                            {
                                canGroup = false;
                                break;
                            }
                            addresses.push_back(address_for_document(document, objectId, std::nullopt,
                                                                     std::nullopt,
                                                                     {std::string("position")}));
                            if (pendingGestureKind_ == SceneGestureKind::Scale)
                                addresses.push_back(address_for_document(document, objectId, std::nullopt,
                                                                         std::nullopt,
                                                                         {std::string("scale")}));
                            else if (pendingGestureKind_ == SceneGestureKind::Rotation)
                                addresses.push_back(address_for_document(document, objectId, std::nullopt,
                                                                         std::nullopt,
                                                                         {std::string("rotation")}));
                            worldPositions.push_back(position);
                            scales.push_back(scale_of(*object));
                            rotations.push_back(rotation_of(*object));
                        }
                    }
                    if (roots.size() > 1)
                    {
                        if (canGroup)
                        {
                            batchGesture_ = commands.begin_batch_gesture(document, addresses, selectionState);
                            if (batchGesture_)
                            {
                                gestureObjectIds_ = roots;
                                gestureStartWorldPositions_ = std::move(worldPositions);
                                gestureStartScales_ = std::move(scales);
                                gestureStartRotations_ = std::move(rotations);
                                groupTransformGesture_ = true;
                            }
                        }
                    }
                    else if (pendingGestureKind_ == SceneGestureKind::Position)
                        gesture_ = begin_object_position_gesture(commands, document, pendingDragObjectId_,
                                                                 selectionState);
                    else if (pendingGestureKind_ == SceneGestureKind::Scale)
                        gesture_ = commands.begin_gesture(document,
                                                          address_for_document(document, pendingDragObjectId_, std::nullopt,
                                                                               std::nullopt, {std::string("scale")}),
                                                          selectionState);
                    else
                        gesture_ = commands.begin_gesture(document,
                                                          address_for_document(document, pendingDragObjectId_, std::nullopt,
                                                                               std::nullopt, {std::string("rotation")}),
                                                          selectionState);
                    break;
                }
                case SceneGestureKind::ColliderOffset:
                    gesture_ = begin_collider_offset_gesture(commands, document, pendingDragObjectId_,
                                                             selectionState);
                    break;
                case SceneGestureKind::RectangleWidth:
                case SceneGestureKind::RectangleHeight:
                {
                    const auto roots = gesture_roots(currentSelection);
                    bool groupRectangle = roots.size() > 1 &&
                                          std::find(roots.begin(), roots.end(), pendingDragObjectId_) != roots.end();
                    std::vector<PropertyAddress> addresses;
                    std::vector<float> sizes;
                    std::vector<float> scales;
                    std::vector<float> rotations;
                    std::vector<ViewportPoint> offsets;
                    std::vector<ViewportPoint> axes;
                    std::size_t componentOccurrence = 0;
                    int componentType = -1;
                    if (groupRectangle)
                    {
                        const auto location = document.indexes.componentsById.find(
                            pendingComponentId_.value_or(-1));
                        const Json *primary = resolve_object(document, pendingDragObjectId_);
                        if (location == document.indexes.componentsById.end() || primary == nullptr)
                            groupRectangle = false;
                        else
                        {
                            const auto components = primary->find("components");
                            if (components == primary->end() || !components->is_array() ||
                                location->second.componentIndex >= components->size())
                                groupRectangle = false;
                            else
                            {
                                componentType = (*components)[location->second.componentIndex].value("type", -1);
                                for (std::size_t index = 0; index < location->second.componentIndex; ++index)
                                    if ((*components)[index].is_object() &&
                                        (*components)[index].value("type", -1) == componentType)
                                        ++componentOccurrence;
                            }
                        }
                    }
                    if (groupRectangle)
                    {
                        addresses.reserve(roots.size() * (pendingRectangleKeepsOppositeEdge_ ? 3U : 1U));
                        sizes.reserve(roots.size());
                        scales.reserve(roots.size());
                        rotations.reserve(roots.size());
                        offsets.reserve(roots.size());
                        axes.reserve(roots.size());
                        const bool widthAxis = pendingGestureKind_ == SceneGestureKind::RectangleWidth;
                        const ViewportPoint primaryRotationAxis = rotate(
                            widthAxis ? ViewportPoint{1.0f, 0.0f} : ViewportPoint{0.0f, 1.0f},
                            rotation_of(*resolve_object(document, pendingDragObjectId_)) *
                                180.0f / std::numbers::pi_v<float>);
                        const float sign = pendingAxisWorld_.x * primaryRotationAxis.x +
                                                       pendingAxisWorld_.y * primaryRotationAxis.y <
                                                   0.0f
                                               ? -1.0f
                                               : 1.0f;
                        for (const int objectId : roots)
                        {
                            const auto matching = component_at_type_occurrence(
                                document, objectId, componentType, componentOccurrence);
                            const Json *object = resolve_object(document, objectId);
                            if (!matching || object == nullptr)
                            {
                                groupRectangle = false;
                                break;
                            }
                            const auto size = finite_number_property(*matching->second,
                                                                     pendingProperty_.c_str());
                            if (!size)
                            {
                                groupRectangle = false;
                                break;
                            }
                            const ViewportPoint objectScale = scale_of(*object);
                            const bool radius = pendingProperty_ == "radius";
                            const float scale = radius
                                                    ? std::max(std::abs(objectScale.x), std::abs(objectScale.y))
                                                    : std::abs(widthAxis ? objectScale.x : objectScale.y);
                            const float rotation = rotation_of(*object);
                            ViewportPoint offset{};
                            if (pendingRectangleKeepsOppositeEdge_)
                            {
                                const auto offsetValue = matching->second->find("offset");
                                if (offsetValue == matching->second->end() || !offsetValue->is_object())
                                {
                                    groupRectangle = false;
                                    break;
                                }
                                const auto offsetX = finite_number_property(*offsetValue, "x");
                                const auto offsetY = finite_number_property(*offsetValue, "y");
                                if (!offsetX || !offsetY)
                                {
                                    groupRectangle = false;
                                    break;
                                }
                                offset = {*offsetX, *offsetY};
                            }
                            const ViewportPoint localAxis = widthAxis
                                                                ? ViewportPoint{sign, 0.0f}
                                                                : ViewportPoint{0.0f, sign};
                            const ViewportPoint worldAxis = normalized(rotate(
                                localAxis, rotation * 180.0f / std::numbers::pi_v<float>));
                            addresses.push_back(address_for_document(document, objectId, matching->first,
                                                                     std::nullopt, {pendingProperty_}));
                            if (pendingRectangleKeepsOppositeEdge_)
                            {
                                addresses.push_back(address_for_document(document, objectId, matching->first,
                                                                         std::nullopt, {std::string("offset"), std::string("x")}));
                                addresses.push_back(address_for_document(document, objectId, matching->first,
                                                                         std::nullopt, {std::string("offset"), std::string("y")}));
                            }
                            sizes.push_back(*size);
                            scales.push_back(std::max(0.0001f, scale));
                            rotations.push_back(rotation);
                            offsets.push_back(offset);
                            axes.push_back(worldAxis);
                        }
                    }
                    if (roots.size() > 1 && groupRectangle)
                    {
                        batchGesture_ = commands.begin_batch_gesture(document, addresses, selectionState);
                        if (batchGesture_)
                        {
                            gestureObjectIds_ = roots;
                            gestureRectangleStartSizes_ = std::move(sizes);
                            gestureRectangleScales_ = std::move(scales);
                            gestureRectangleRotations_ = std::move(rotations);
                            gestureRectangleOffsets_ = std::move(offsets);
                            gestureRectangleAxes_ = std::move(axes);
                            groupRectangleGesture_ = true;
                        }
                    }
                    else if (roots.size() <= 1 || !groupRectangle)
                    {
                        const PropertyAddress dimensionAddress = address_for_document(
                            document, pendingDragObjectId_, pendingComponentId_, std::nullopt,
                            {pendingProperty_});
                        addresses = {dimensionAddress};
                        if (pendingRectangleKeepsOppositeEdge_)
                        {
                            addresses.push_back(address_for_document(document, pendingDragObjectId_,
                                                                     pendingComponentId_, std::nullopt,
                                                                     {std::string("offset"), std::string("x")}));
                            addresses.push_back(address_for_document(document, pendingDragObjectId_,
                                                                     pendingComponentId_, std::nullopt,
                                                                     {std::string("offset"), std::string("y")}));
                        }
                        batchGesture_ = commands.begin_batch_gesture(document, addresses, selectionState);
                    }
                    break;
                }
                }
                if (gesture_ || batchGesture_)
                {
                    gestureAddress_.documentGeneration = document.documentGeneration;
                    gestureAddress_.identity = DocumentIdentity{
                        ProjectGeneration{document.projectGeneration}, document.tabId,
                        DocumentGeneration{document.documentGeneration}};
                    gestureAddress_.objectId = pendingDragObjectId_;
                    gestureKind_ = pendingGestureKind_;
                    gesturePointerWorld_ = pendingPointerWorld_;
                    gestureStartWorld_ = pendingTargetWorld_;
                    gestureAxisWorld_ = pendingAxisWorld_;
                    gestureAxisScreen_ = pendingAxisScreen_;
                    gestureScaleAxis_ = pendingScaleAxis_;
                    gestureStartRectangleSize_ = pendingStartRectangleSize_;
                    gestureRectangleMultiplier_ = pendingRectangleMultiplier_;
                    gestureStartRectangleOffset_ = pendingRectangleOffset_;
                    gestureRectangleKeepsOppositeEdge_ = pendingRectangleKeepsOppositeEdge_;
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

        if (gesture_active())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ViewportPoint currentWorld = world_at_screen(mouse);
                ViewportPoint delta{currentWorld.x - gesturePointerWorld_.x,
                                    currentWorld.y - gesturePointerWorld_.y};
                Json value;
                std::vector<Json> groupGestureValues;
                switch (gestureKind_)
                {
                case SceneGestureKind::Position:
                {
                    const float axisLengthSquared = gestureAxisWorld_.x * gestureAxisWorld_.x +
                                                    gestureAxisWorld_.y * gestureAxisWorld_.y;
                    if (axisLengthSquared > 0.0001f)
                    {
                        const float amount = (delta.x * gestureAxisWorld_.x +
                                              delta.y * gestureAxisWorld_.y) /
                                             axisLengthSquared;
                        delta = {gestureAxisWorld_.x * amount, gestureAxisWorld_.y * amount};
                    }
                    if (groupTransformGesture_ && gestureObjectIds_.size() > 1 &&
                        gestureStartWorldPositions_.size() == gestureObjectIds_.size())
                    {
                        ViewportPoint effectiveDelta = delta;
                        ViewportPoint desiredPivot{gestureStartWorld_.x + delta.x,
                                                   gestureStartWorld_.y + delta.y};
                        if (io.KeyCtrl && snapPreferences.coordinateMode == "world")
                        {
                            desiredPivot.x = snap_scene_value(desiredPivot.x, snapPreferences.position);
                            desiredPivot.y = snap_scene_value(desiredPivot.y, snapPreferences.position);
                            effectiveDelta = {desiredPivot.x - gestureStartWorld_.x,
                                              desiredPivot.y - gestureStartWorld_.y};
                        }
                        else if (io.KeyCtrl && snapPreferences.coordinateMode == "local")
                        {
                            const Json *primary = resolve_object(document, gestureAddress_.objectId);
                            const auto desiredLocal = object_local_position_for_world(
                                document, gestureAddress_.objectId, desiredPivot);
                            if (primary != nullptr && desiredLocal)
                            {
                                const ViewportPoint initialLocal = position_of(*primary);
                                const ViewportPoint snappedLocal{
                                    snap_scene_value(desiredLocal->x, snapPreferences.position),
                                    snap_scene_value(desiredLocal->y, snapPreferences.position)};
                                effectiveDelta = {snappedLocal.x - initialLocal.x,
                                                  snappedLocal.y - initialLocal.y};
                            }
                        }
                        groupGestureValues.reserve(gestureObjectIds_.size());
                        for (std::size_t index = 0; index < gestureObjectIds_.size(); ++index)
                        {
                            const auto local = object_local_position_for_world(
                                document, gestureObjectIds_[index],
                                {gestureStartWorldPositions_[index].x + effectiveDelta.x,
                                 gestureStartWorldPositions_[index].y + effectiveDelta.y});
                            if (!local || !std::isfinite(local->x) || !std::isfinite(local->y))
                            {
                                groupGestureValues.clear();
                                break;
                            }
                            groupGestureValues.emplace_back(Json{{"x", local->x}, {"y", local->y}});
                        }
                        if (groupGestureValues.size() == gestureObjectIds_.size())
                            value = groupGestureValues.front();
                    }
                    else
                    {
                        ViewportPoint desiredWorld{gestureStartWorld_.x + delta.x,
                                                   gestureStartWorld_.y + delta.y};
                        if (io.KeyCtrl && snapPreferences.coordinateMode == "world")
                        {
                            desiredWorld.x = snap_scene_value(desiredWorld.x, snapPreferences.position);
                            desiredWorld.y = snap_scene_value(desiredWorld.y, snapPreferences.position);
                        }
                        auto local = object_local_position_for_world(document, gestureAddress_.objectId, desiredWorld);
                        if (local)
                        {
                            if (io.KeyCtrl && snapPreferences.coordinateMode == "local")
                            {
                                local->x = snap_scene_value(local->x, snapPreferences.position);
                                local->y = snap_scene_value(local->y, snapPreferences.position);
                            }
                            value = Json{{"x", local->x}, {"y", local->y}};
                        }
                    }
                    break;
                }
                case SceneGestureKind::ColliderOffset:
                {
                    const auto localOffset = collider_offset_for_world(
                        document, gestureAddress_.objectId,
                        {gestureStartWorld_.x + delta.x, gestureStartWorld_.y + delta.y});
                    if (localOffset)
                        value = Json{{"x", io.KeyCtrl ? snap_scene_value(localOffset->x, snapPreferences.position) : localOffset->x},
                                     {"y", io.KeyCtrl ? snap_scene_value(localOffset->y, snapPreferences.position) : localOffset->y}};
                    break;
                }
                case SceneGestureKind::Scale:
                {
                    const float pixelDelta = (mouse.x - pendingPressScreen_.x) * gestureAxisScreen_.x +
                                             (mouse.y - pendingPressScreen_.y) * gestureAxisScreen_.y;
                    float factor = 1.0f + pixelDelta / 62.0f;
                    if (groupTransformGesture_ && gestureObjectIds_.size() > 1 &&
                        gestureStartWorldPositions_.size() == gestureObjectIds_.size() &&
                        gestureStartScales_.size() == gestureObjectIds_.size())
                    {
                        const bool xAxis = gestureScaleAxis_ == 0;
                        const float primaryScale = xAxis ? gestureStartScale_.x : gestureStartScale_.y;
                        const float targetPrimaryScale = std::max(0.01f, primaryScale * factor);
                        if (io.KeyCtrl && primaryScale > 0.0f)
                            factor = std::max(0.01f, snap_scene_value(targetPrimaryScale,
                                                                      snapPreferences.scale)) /
                                     primaryScale;
                        const float axisLength = std::sqrt(gestureAxisWorld_.x * gestureAxisWorld_.x +
                                                           gestureAxisWorld_.y * gestureAxisWorld_.y);
                        if (!std::isfinite(factor) || axisLength <= 0.0001f)
                            break;
                        const ViewportPoint axis{gestureAxisWorld_.x / axisLength,
                                                 gestureAxisWorld_.y / axisLength};
                        groupGestureValues.reserve(gestureObjectIds_.size() * 2);
                        for (std::size_t index = 0; index < gestureObjectIds_.size(); ++index)
                        {
                            const ViewportPoint start = gestureStartWorldPositions_[index];
                            const float relativeAxis = (start.x - gestureStartWorld_.x) * axis.x +
                                                       (start.y - gestureStartWorld_.y) * axis.y;
                            const ViewportPoint targetWorld{
                                start.x + axis.x * relativeAxis * (factor - 1.0f),
                                start.y + axis.y * relativeAxis * (factor - 1.0f)};
                            const auto local = object_local_position_for_world(
                                document, gestureObjectIds_[index], targetWorld);
                            if (!local || !std::isfinite(local->x) || !std::isfinite(local->y))
                            {
                                groupGestureValues.clear();
                                break;
                            }
                            ViewportPoint scale = gestureStartScales_[index];
                            if (xAxis)
                                scale.x = std::max(0.01f, scale.x * factor);
                            else
                                scale.y = std::max(0.01f, scale.y * factor);
                            groupGestureValues.emplace_back(Json{{"x", local->x}, {"y", local->y}});
                            groupGestureValues.emplace_back(Json{{"x", scale.x}, {"y", scale.y}});
                        }
                        if (groupGestureValues.size() == gestureObjectIds_.size() * 2)
                            value = groupGestureValues.front();
                        break;
                    }
                    float scaleX = gestureScaleAxis_ == 0
                                       ? std::max(0.01f, gestureStartScale_.x * factor)
                                       : gestureStartScale_.x;
                    float scaleY = gestureScaleAxis_ == 1
                                       ? std::max(0.01f, gestureStartScale_.y * factor)
                                       : gestureStartScale_.y;
                    if (io.KeyCtrl)
                    {
                        if (gestureScaleAxis_ == 0)
                            scaleX = std::max(0.01f, snap_scene_value(scaleX, snapPreferences.scale));
                        else
                            scaleY = std::max(0.01f, snap_scene_value(scaleY, snapPreferences.scale));
                    }
                    value = Json{{"x", scaleX}, {"y", scaleY}};
                    break;
                }
                case SceneGestureKind::Rotation:
                {
                    const float currentAngle = std::atan2(mouse.y - gesturePivotScreen_.y,
                                                          mouse.x - gesturePivotScreen_.x);
                    float angleDelta = std::remainder(currentAngle - gestureStartAngle_,
                                                      2.0f * std::numbers::pi_v<float>);
                    float rotation = gestureStartRotation_ + angleDelta;
                    if (io.KeyCtrl)
                    {
                        rotation = snap_scene_value(rotation * 180.0f / std::numbers::pi_v<float>,
                                                    snapPreferences.rotationDegrees) *
                                   std::numbers::pi_v<float> / 180.0f;
                        angleDelta = rotation - gestureStartRotation_;
                    }
                    if (groupTransformGesture_ && gestureObjectIds_.size() > 1 &&
                        gestureStartWorldPositions_.size() == gestureObjectIds_.size() &&
                        gestureStartRotations_.size() == gestureObjectIds_.size())
                    {
                        groupGestureValues.reserve(gestureObjectIds_.size() * 2);
                        for (std::size_t index = 0; index < gestureObjectIds_.size(); ++index)
                        {
                            const ViewportPoint start = gestureStartWorldPositions_[index];
                            const ViewportPoint relative{start.x - gestureStartWorld_.x,
                                                         start.y - gestureStartWorld_.y};
                            const ViewportPoint rotated = rotate(relative,
                                                                 angleDelta * 180.0f / std::numbers::pi_v<float>);
                            const auto local = object_local_position_for_world(
                                document, gestureObjectIds_[index],
                                {gestureStartWorld_.x + rotated.x, gestureStartWorld_.y + rotated.y});
                            if (!local || !std::isfinite(local->x) || !std::isfinite(local->y))
                            {
                                groupGestureValues.clear();
                                break;
                            }
                            groupGestureValues.emplace_back(Json{{"x", local->x}, {"y", local->y}});
                            groupGestureValues.emplace_back(gestureStartRotations_[index] + angleDelta);
                        }
                        if (groupGestureValues.size() == gestureObjectIds_.size() * 2)
                            value = groupGestureValues.front();
                    }
                    else
                        value = rotation;
                    break;
                }
                case SceneGestureKind::RectangleWidth:
                case SceneGestureKind::RectangleHeight:
                {
                    if (groupRectangleGesture_ && gestureObjectIds_.size() > 1 &&
                        gestureRectangleStartSizes_.size() == gestureObjectIds_.size() &&
                        gestureRectangleScales_.size() == gestureObjectIds_.size() &&
                        gestureRectangleAxes_.size() == gestureObjectIds_.size() &&
                        gestureRectangleRotations_.size() == gestureObjectIds_.size() &&
                        gestureRectangleOffsets_.size() == gestureObjectIds_.size())
                    {
                        groupGestureValues.reserve(gestureObjectIds_.size() *
                                                   (gestureRectangleKeepsOppositeEdge_ ? 3U : 1U));
                        for (std::size_t index = 0; index < gestureObjectIds_.size(); ++index)
                        {
                            const float signedDelta = delta.x * gestureRectangleAxes_[index].x +
                                                      delta.y * gestureRectangleAxes_[index].y;
                            float dimension = resized_rectangle_dimension(
                                gestureRectangleStartSizes_[index], signedDelta,
                                gestureRectangleScales_[index], gestureRectangleMultiplier_);
                            if (io.KeyCtrl)
                                dimension = std::max(0.01f,
                                                     snap_scene_value(dimension, snapPreferences.position));
                            if (!std::isfinite(dimension))
                            {
                                groupGestureValues.clear();
                                break;
                            }
                            groupGestureValues.emplace_back(dimension);
                            if (gestureRectangleKeepsOppositeEdge_)
                            {
                                const ViewportPoint offset = rectangle_offset_after_edge_resize(
                                    gestureRectangleOffsets_[index], gestureRectangleAxes_[index],
                                    gestureRectangleRotations_[index] * 180.0f /
                                        std::numbers::pi_v<float>,
                                    gestureRectangleStartSizes_[index], dimension,
                                    gestureRectangleScales_[index]);
                                groupGestureValues.emplace_back(offset.x);
                                groupGestureValues.emplace_back(offset.y);
                            }
                        }
                        const std::size_t expected = gestureObjectIds_.size() *
                                                     (gestureRectangleKeepsOppositeEdge_ ? 3U : 1U);
                        if (groupGestureValues.size() == expected)
                            value = groupGestureValues.front();
                    }
                    else
                    {
                        const float signedDelta = delta.x * gestureAxisWorld_.x + delta.y * gestureAxisWorld_.y;
                        float dimension = resized_rectangle_dimension(gestureStartRectangleSize_, signedDelta,
                                                                      pendingRectangleScale_,
                                                                      gestureRectangleMultiplier_);
                        if (io.KeyCtrl)
                            dimension = std::max(0.01f, snap_scene_value(dimension, snapPreferences.position));
                        if (std::isfinite(dimension))
                            value = dimension;
                    }
                    break;
                }
                }
                if (value.is_null())
                    cancel_gesture(commands, document);
                else
                {
                    CommandResult update;
                    if (batchGesture_ && batchGesture_->active())
                    {
                        std::vector<Json> values;
                        if (groupTransformGesture_ || groupRectangleGesture_)
                            values = std::move(groupGestureValues);
                        else
                            values = {value};
                        if (!groupRectangleGesture_ && gestureRectangleKeepsOppositeEdge_)
                        {
                            const float dimension = value.get<float>();
                            const auto offset = rectangle_offset_after_edge_resize(
                                gestureStartRectangleOffset_, gestureAxisWorld_,
                                gestureStartRotation_ * 180.0f / std::numbers::pi_v<float>,
                                gestureStartRectangleSize_, dimension, pendingRectangleScale_);
                            values.emplace_back(offset.x);
                            values.emplace_back(offset.y);
                        }
                        update = batchGesture_->update(commands, document, values);
                    }
                    else if (gesture_ && gesture_->active())
                        update = gesture_->update(commands, document, std::move(value));
                    else
                        update.status = CommandStatus::Rejected;
                    if (!update)
                        cancel_gesture(commands, document);
                }
            }
            else
                commit_gesture();
        }

        const bool panButtonHeld = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                                   ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                                   (tool_ == SceneTool::Hand && ImGui::IsMouseDown(ImGuiMouseButton_Left)) ||
                                   (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        if (panning_ && panButtonHeld)
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

        if (panning_ || (imageHovered && (tool_ == SceneTool::Hand || io.KeyAlt)))
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        if (imageHovered && !capture && !gesture_active())
        {
            const float wheelStep = std::clamp(io.MouseWheel, -1.0f, 1.0f);
            if (wheelStep != 0.0f && transform_.zoom_at(mouse, std::pow(1.1f, wheelStep)))
            {
                viewZoom_ = transform_.zoom();
                viewCenter_ = {transform_.camera_left() + transform_.world_width() * 0.5f,
                               transform_.camera_top() + transform_.world_height() * 0.5f};
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                (tool_ == SceneTool::Hand && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
                (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
            {
                panning_ = true;
                panStart_ = mouse;
                ImGui::SetWindowFocus();
                Selection selected = selection.current();
                selected.focusedPanel = FocusedPanel::Scene;
                selection.set(document, std::move(selected));
            }

            if (tool_ != SceneTool::Hand && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                Selection selected = selection.current();
                selected.focusedPanel = FocusedPanel::Scene;
                const auto worldPoint = transform_.screen_to_world(mouse);
                bool startedOnGizmo = false;
                if (editingEnabled && selected.objectId)
                {
                    if (tool_ == SceneTool::Rectangle)
                    {
                        const auto handle = hit_rectangle_handle(document, preview, selected, transform_, mouse,
                                                                 uiScale);
                        if (handle)
                        {
                            selection.set(document, selected);
                            pendingGestureKind_ = handle->kind;
                            pendingDragObjectId_ = *selected.objectId;
                            pendingComponentId_ = handle->componentId;
                            pendingProperty_ = handle->property;
                            pendingStartRectangleSize_ = handle->initialValue;
                            pendingRectangleScale_ = handle->scale;
                            pendingRectangleMultiplier_ = handle->edgeMultiplier;
                            pendingRectangleOffset_ = handle->initialOffset;
                            pendingRectangleKeepsOppositeEdge_ = handle->preserveOppositeEdge;
                            pendingPressScreen_ = mouse;
                            pendingPointerWorld_ = worldPoint.value_or(object_world_position(document, *selected.objectId));
                            pendingTargetWorld_ = object_world_position(document, *selected.objectId);
                            pendingAxisWorld_ = handle->outwardAxis;
                            pendingAxisScreen_ = {};
                            pendingScaleAxis_ = -1;
                            pendingPivotScreen_ = transform_.world_to_screen(pendingTargetWorld_);
                            pendingDrag_ = true;
                            startedOnGizmo = true;
                        }
                    }
                    else if (const Json *object = resolve_object(document, *selected.objectId))
                    {
                        const ViewportPoint pivot = object_world_position(document, *selected.objectId);
                        const float rotationDegrees = rotation_of(*object) * 180.0f /
                                                      std::numbers::pi_v<float>;
                        const auto handle = hit_transform_handle(transform_, pivot,
                                                                 rotationDegrees, mouse, tool_, uiScale);
                        if (handle)
                        {
                            selection.set(document, selected);
                            pendingGestureKind_ = handle->kind;
                            pendingDragObjectId_ = *selected.objectId;
                            pendingComponentId_.reset();
                            pendingProperty_.clear();
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
                    static const std::vector<int> noVisibilityOverrides;
                    const auto &hiddenList = hiddenObjectIds == nullptr
                                                 ? noVisibilityOverrides
                                                 : *hiddenObjectIds;
                    const auto &lockedList = lockedObjectIds == nullptr
                                                 ? noVisibilityOverrides
                                                 : *lockedObjectIds;
                    const auto pickableIds = viewport_pickable_ids(
                        preview.ordered_authoring_ids(), hiddenList, lockedList);
                    const auto candidates = viewport_pick_targets(transform_, pickableIds, preview, mouse,
                                                                  8.0f * uiScale);
                    if (candidates.empty())
                    {
                        const bool additive = io.KeyShift || io.KeyCtrl;
                        if (!additive)
                        {
                            selected.objectId.reset();
                            selected.componentId.reset();
                            selected.behaviorOrdinal.reset();
                            selected.selectedObjectIds.clear();
                            selected.rangeAnchorObjectId.reset();
                        }
                        pickCycle_.reset();
                        if (tool_ != SceneTool::Hand)
                        {
                            marquee_.active = true;
                            marquee_.additive = additive;
                            marquee_.startScreen = mouse;
                            marquee_.currentScreen = mouse;
                        }
                    }
                    else
                    {
                        const auto target = pickCycle_.choose(candidates, mouse);
                        const bool targetWasSelected = target &&
                                                       std::find(selected.selectedObjectIds.begin(), selected.selectedObjectIds.end(),
                                                                 target->objectId) != selected.selectedObjectIds.end();
                        if (!targetWasSelected)
                        {
                            selected.selectedObjectIds.clear();
                            selected.rangeAnchorObjectId = target
                                                               ? std::optional<int>(target->objectId)
                                                               : std::nullopt;
                        }
                        selected.objectId = target ? std::optional<int>(target->objectId) : std::nullopt;
                        selected.componentId = target ? target->componentId : std::nullopt;
                        selected.behaviorOrdinal.reset();
                    }
                    selection.set(document, selected);
                    pendingDrag_ = false;
                    if (editingEnabled && tool_ == SceneTool::Move && worldPoint && selected.objectId)
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
                                                      ? SceneGestureKind::ColliderOffset
                                                      : SceneGestureKind::Position;
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
        if (!panButtonHeld)
            panning_ = false;

        if (capture)
            panning_ = false;
        ImGui::End();
    }

    std::vector<SemanticNode> SceneViewportPanel::semantic_nodes(
        const DocumentSession &document, const Selection &selection,
        const SnapPreferences &snapPreferences, std::string_view parentId) const
    {
        const std::string rootId(parentId);
        const auto make_node = [&](std::string suffix, SemanticRole role, std::string name)
        {
            SemanticNode node;
            node.id = rootId + (suffix.empty() ? std::string{} : "/" + suffix);
            node.parentId = suffix.empty() ? std::string{} : rootId;
            node.role = role;
            node.name = std::move(name);
            node.visible = true;
            return node;
        };
        std::vector<SemanticNode> nodes;
        nodes.reserve(12);

        auto root = make_node({}, SemanticRole::Window, "Scene");
        root.expanded = true;
        nodes.push_back(std::move(root));

        auto canvas = make_node("canvas", SemanticRole::Group, "Scene canvas");
        canvas.value = transform_.valid() ? "2D authoring preview" : "Scene preview unavailable";
        canvas.help = "The Scene canvas edits authored data; it does not run behaviors or simulate gameplay.";
        if (transform_.valid())
        {
            const auto &rect = transform_.image_rect();
            canvas.bounds = SemanticBounds{rect.x, rect.y, rect.width, rect.height};
        }
        nodes.push_back(std::move(canvas));

        const std::size_t selectedCount = selection.selectedObjectIds.empty()
                                              ? static_cast<std::size_t>(selection.objectId.has_value())
                                              : selection.selectedObjectIds.size();
        auto selected = make_node("selection", SemanticRole::Group,
                                  selection.objectId ? "Scene selection" : "No Scene object selected");
        selected.value = std::to_string(selectedCount) +
                         (selectedCount == 1 ? " GameObject selected" : " GameObjects selected");
        if (selection.objectId)
            if (const Json *object = resolve_object(document, *selection.objectId))
                selected.help = object->value("name", std::string("GameObject"));
        selected.selected = selection.objectId.has_value();
        selected.focused = selection.focusedPanel == FocusedPanel::Scene;
        nodes.push_back(std::move(selected));

        const bool toolActionsEnabled = !has_active_input();
        const auto add_tool = [&](std::string suffix, SceneTool tool, std::string name)
        {
            auto node = make_node("tool/" + suffix, SemanticRole::Button, std::move(name));
            node.selected = tool_ == tool;
            node.value = node.selected ? "active" : "inactive";
            node.enabled = toolActionsEnabled;
            node.actions = {"activate"};
            nodes.push_back(std::move(node));
        };
        add_tool("hand", SceneTool::Hand, "Hand tool (Q)");
        add_tool("move", SceneTool::Move, "Move tool (W)");
        add_tool("rotate", SceneTool::Rotate, "Rotate tool (E)");
        add_tool("scale", SceneTool::Scale, "Scale tool (R)");
        add_tool("rectangle", SceneTool::Rectangle, "Rectangle sizing tool (T)");

        auto frameAll = make_node("frame-all", SemanticRole::Button, "Frame all Scene objects");
        frameAll.actions = {"activate"};
        nodes.push_back(std::move(frameAll));
        auto frameSelected = make_node("frame-selected", SemanticRole::Button,
                                       "Frame selected Scene objects");
        frameSelected.enabled = selection.objectId &&
                                document.indexes.objectsById.contains(*selection.objectId);
        frameSelected.actions = {"activate"};
        nodes.push_back(std::move(frameSelected));

        const auto add_snap_value = [&](std::string suffix, std::string name, float value)
        {
            auto node = make_node("snap/" + suffix, SemanticRole::Text, std::move(name));
            node.value = std::to_string(value);
            nodes.push_back(std::move(node));
        };
        add_snap_value("position", "Position snap increment", snapPreferences.position);
        add_snap_value("rotation", "Rotation snap increment in degrees",
                       snapPreferences.rotationDegrees);
        add_snap_value("scale", "Scale snap increment", snapPreferences.scale);
        auto coordinates = make_node("snap/coordinates", SemanticRole::Text,
                                     "Position snap coordinate space");
        coordinates.value = snapPreferences.coordinateMode == "local" ? "Local" : "World";
        nodes.push_back(std::move(coordinates));

        auto zoom = make_node("zoom", SemanticRole::Text, "Scene zoom");
        zoom.value = std::to_string(static_cast<int>(std::round(viewZoom_ * 100.0f))) + "%";
        nodes.push_back(std::move(zoom));
        return nodes;
    }

    bool SceneViewportPanel::dispatch_semantic_action(const SemanticAction &action,
                                                      const DocumentSession &document,
                                                      const Selection &selection)
    {
        if (action.action != "activate" || has_active_input())
            return false;
        if (action.nodeId == "main/panel/scene/frame-all")
        {
            request_frame_all();
            return true;
        }
        if (action.nodeId == "main/panel/scene/frame-selected")
        {
            if (!selection.objectId || !document.indexes.objectsById.contains(*selection.objectId))
                return false;
            request_frame_selected();
            return true;
        }
        if (action.nodeId == "main/panel/scene/tool/hand")
            set_tool(SceneTool::Hand);
        else if (action.nodeId == "main/panel/scene/tool/move")
            set_tool(SceneTool::Move);
        else if (action.nodeId == "main/panel/scene/tool/rotate")
            set_tool(SceneTool::Rotate);
        else if (action.nodeId == "main/panel/scene/tool/scale")
            set_tool(SceneTool::Scale);
        else if (action.nodeId == "main/panel/scene/tool/rectangle")
            set_tool(SceneTool::Rectangle);
        else
            return false;
        return true;
    }
} // namespace platformator_editor
