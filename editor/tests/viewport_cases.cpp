#include "document.h"
#include "editcommands.h"
#include "preview.h"
#include "viewport.h"

#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace platformator_editor
{
namespace
{
void require_case(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Json object(int id, float x, float y, Json components = Json::array(), Json children = Json::array(),
            float rotation = 0.0f)
{
    return Json{{"id", id}, {"name", "viewport"}, {"tag", ""}, {"active", true},
                {"position", {{"x", x}, {"y", y}}}, {"scale", {{"x", 1.0}, {"y", 1.0}}},
                {"rotation", rotation}, {"children", std::move(children)}, {"components", std::move(components)}};
}

Json component(int id, int type, Json fields)
{
    fields["id"] = id;
    fields["type"] = type;
    return fields;
}

Json sprite(int componentId)
{
    return component(componentId, 6,
                     Json{{"textureFilePath", "viewport-missing.png"}, {"flip", 0},
                          {"width", 40.0}, {"height", 20.0}, {"sourceRectEnabled", false}});
}

void transform_cases()
{
    ViewportTransform transform;
    require_case(!transform.configure({20.0f, 30.0f, 0.0f, 100.0f}, 2.0f, 3.0f,
                                      320.0f, 160.0f, 0.0f, 0.0f),
                 "zero-sized Scene content must suspend the transform");
    require_case(transform.configure({100.0f, 50.0f, 360.0f, 250.0f}, 2.0f, 3.0f,
                                     320.0f, 160.0f, 100.0f, 50.0f),
                 "off-origin Scene image must configure");
    require_case(transform.target_width() == 720 && transform.target_height() == 540,
                 "framebuffer scale must use real x/y output ratios independently of style scale");
    const auto content = transform.content_rect();
    require_case(std::abs(content.x - 100.0f) < 0.001f && std::abs(content.y - 85.0f) < 0.001f &&
                     std::abs(content.width - 360.0f) < 0.001f && std::abs(content.height - 180.0f) < 0.001f,
                 "the camera aspect must letterbox inside the actual Scene image rectangle");
    const auto center = transform.screen_to_world({280.0f, 175.0f});
    require_case(center && std::abs(center->x - 100.0f) < 0.001f && std::abs(center->y - 50.0f) < 0.001f,
                 "screen/world mapping must respect the image's off-origin position");
    require_case(!transform.screen_to_world({150.0f, 60.0f}),
                 "letterbox margins must not map to world coordinates");
    const ViewportPoint zoomAnchor{210.0f, 140.0f};
    const auto beforeZoom = transform.screen_to_world(zoomAnchor);
    require_case(beforeZoom.has_value() && transform.zoom_at(zoomAnchor, 2.0f),
                 "pointer anchored zoom must accept a valid point");
    const auto afterZoom = transform.screen_to_world(zoomAnchor);
    require_case(afterZoom && std::abs(afterZoom->x - beforeZoom->x) < 0.001f &&
                     std::abs(afterZoom->y - beforeZoom->y) < 0.001f,
                 "zoom must preserve the world point beneath the pointer");
    transform.pan(36.0f, -18.0f);
    const auto moved = transform.world_to_screen(*afterZoom);
    require_case(std::abs(moved.x - (zoomAnchor.x + 36.0f)) < 0.001f &&
                     std::abs(moved.y - (zoomAnchor.y - 18.0f)) < 0.001f,
                 "pan must follow logical pointer displacement without changing saved camera data");
    require_case(transform.frame_bounds({20.0f, 30.0f, 40.0f, 20.0f}) &&
                     transform.world_width() >= 44.0f && transform.world_height() >= 22.0f,
                 "Frame Selection must fit the supplied bounds with padding");
}

void picking_and_gesture_cases()
{
    RuntimeOptions options;
    options.windowSettings.width = 320;
    options.windowSettings.height = 240;
    options.assetRoot = std::filesystem::path(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR) / "assets";
    platformator::Runtime runtime(options);
    DocumentSession document = make_default_document(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR,
        std::filesystem::path(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR) / "assets", 19);
    Json child = object(12, -25.0f, -30.0f,
                        Json::array({sprite(13), component(14, 3, Json{{"colliderType", 1},
                                                                         {"width", 20.0}, {"height", 12.0}})}),
                        Json::array(), std::numbers::pi_v<float> * 0.5f);
    Json parent = object(10, 25.0f, 30.0f, Json::array(), Json::array({child}));
    Json top = object(20, 0.0f, 0.0f, Json::array({sprite(21)}));
    top["active"] = false;
    document.root.push_back(parent);
    document.root.push_back(top);
    document.structureRevision++;
    rebuild_document_indexes(document);

    EditorPreview preview(runtime);
    preview.reconcile(document, true);
    const auto *childInfo = preview.object_info(12);
    require_case(childInfo && childInfo->worldX == 0.0f && childInfo->worldY == 0.0f,
                 "child world position must add parent translation only");
    ViewportTransform transform;
    require_case(transform.configure({100.0f, 50.0f, 640.0f, 480.0f}, 1.0f, 1.0f,
                                     320.0f, 240.0f, 0.0f, 0.0f),
                 "picking transform must configure");
    const auto overlapping = viewport_pick_candidates(transform, preview.ordered_document_ids(), preview,
                                                       {420.0f, 290.0f}, 0.0f); // image center maps to world (0, 0)
    require_case(overlapping.size() == 2 && overlapping[0] == 20 && overlapping[1] == 12,
                 "overlapping targets must cycle in stable topmost draw order, including missing assets");
    ViewportPickCycle cycle;
    require_case(cycle.choose(overlapping, {420.0f, 290.0f}) == 20 &&
                     cycle.choose(overlapping, {421.0f, 290.0f}) == 12 &&
                     cycle.choose(overlapping, {420.0f, 290.0f}) == 20,
                 "repeated overlapping clicks must cycle deterministically through the draw stack");
    const auto localPosition = object_local_position_for_world(document, 12, {40.0f, 60.0f});
    require_case(localPosition && localPosition->x == 15.0f && localPosition->y == 30.0f,
                 "object dragging must convert desired world position to parent-local translation");
    const ViewportPoint colliderCenter{-6.0f, 4.0f};
    const auto localOffset = collider_offset_for_world(document, 12, colliderCenter);
    require_case(localOffset && std::abs(localOffset->x - 4.0f) < 0.001f &&
                     std::abs(localOffset->y - 6.0f) < 0.001f,
                 "collider handle drags must invert object rotation into the serialized local offset");

    const Json originalPosition = document.root[1]["children"][0]["position"];
    EditCommandService commands;
    std::size_t notifications = 0;
    commands.subscribe([&](const DocumentChange &, const std::vector<int> &) { ++notifications; });
    auto equalGesture = begin_object_position_gesture(commands, document, 12);
    require_case(equalGesture.has_value(), "equal-value gesture must begin");
    const auto equalUpdate = equalGesture->update(commands, document, originalPosition);
    require_case(static_cast<bool>(equalUpdate) && !equalGesture->commit(),
                 "committing the initial value must produce no property command");
    auto positionGesture = begin_object_position_gesture(commands, document, 12);
    require_case(positionGesture.has_value(), "object handle drag must begin through the common command service");
    const auto update = positionGesture->update(commands, document, Json{{"x", 90.0}, {"y", 80.0}});
    require_case(static_cast<bool>(update) && document.root[1]["children"][0]["position"]["x"] == 90.0,
                 "object gesture updates must apply through the common property path");
    const auto cancelled = positionGesture->cancel(commands, document);
    require_case(static_cast<bool>(cancelled) && document.root[1]["children"][0]["position"] == originalPosition,
                 "cancelled drags must restore exact initial values");
    require_case(notifications == 2,
                 "gesture updates and cancellation restores must both publish preview change notifications");

    auto offsetGesture = begin_collider_offset_gesture(commands, document, 12);
    require_case(offsetGesture.has_value(), "collider handle drag must begin through the common command service");
    require_case(static_cast<bool>(offsetGesture->update(commands, document, Json{{"x", 4.0}, {"y", 6.0}})),
                 "collider offset gesture must accept a typed vector update");
    require_case(static_cast<bool>(offsetGesture->cancel(commands, document)) &&
                     !document.root[1]["children"][0]["components"][1].contains("offset"),
                 "cancelling a gesture must restore an originally missing key exactly");
    require_case(notifications == 4,
                 "collider update and cancellation must also notify the preview observer");
    preview.release();
}
} // namespace

void run_viewport_cases()
{
    transform_cases();
    picking_and_gesture_cases();
}
} // namespace platformator_editor
