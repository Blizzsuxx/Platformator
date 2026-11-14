#include "document.h"
#include "actions.h"
#include "editcommands.h"
#include "preview.h"
#include "viewport.h"
#include "panels/hierarchy.h"
#include "templates.h"
#include "assetformats.h"

#include <imgui.h>
#include <platformator/runtime.h>
#include <platformator/runtimeoptions.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
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
            return Json{{"id", id}, {"name", "viewport"}, {"tag", ""}, {"active", true}, {"position", {{"x", x}, {"y", y}}}, {"scale", {{"x", 1.0}, {"y", 1.0}}}, {"rotation", rotation}, {"children", std::move(children)}, {"components", std::move(components)}};
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
                             Json{{"textureFilePath", "viewport-missing.png"}, {"flip", 0}, {"width", 40.0}, {"height", 20.0}, {"sourceRectEnabled", false}});
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
            require_case(transform.target_width() == 720 && transform.target_height() == 750,
                         "framebuffer scale must use real x/y output ratios independently of style scale");
            const auto content = transform.content_rect();
            require_case(std::abs(content.x - 100.0f) < 0.001f && std::abs(content.y - 50.0f) < 0.001f &&
                             std::abs(content.width - 360.0f) < 0.001f && std::abs(content.height - 250.0f) < 0.001f,
                         "the authoring image must fill the actual Scene rectangle without letterboxing");
            require_case(std::abs(transform.world_width() - 230.4f) < 0.001f &&
                             std::abs(transform.world_height() - 160.0f) < 0.001f,
                         "world units per logical screen unit must preserve geometry across the full panel aspect");
            const auto center = transform.screen_to_world({280.0f, 175.0f});
            require_case(center && std::abs(center->x - 100.0f) < 0.001f && std::abs(center->y - 50.0f) < 0.001f,
                         "screen/world mapping must respect the image's off-origin position");
            const auto corner = transform.screen_to_world({150.0f, 60.0f});
            require_case(corner && std::abs(corner->x - 16.8f) < 0.001f &&
                             std::abs(corner->y + 23.6f) < 0.001f,
                         "every point inside the full image panel must map through independent world ratios");
            const ViewportPoint knownWorld{145.0f, 90.0f};
            const auto knownScreen = transform.world_to_screen(knownWorld);
            const auto roundTrip = transform.screen_to_world(knownScreen);
            require_case(roundTrip && std::abs(roundTrip->x - knownWorld.x) < 0.001f &&
                             std::abs(roundTrip->y - knownWorld.y) < 0.001f,
                         "world-to-screen and screen-to-world must be reversible at off-center points");
            require_case(!transform.screen_to_world({99.0f, 60.0f}),
                         "points outside the full image rectangle must not map to world coordinates");
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
                             transform.world_width() >= 43.999f && transform.world_height() >= 21.999f,
                         "Frame Selection must fit the supplied bounds with padding");

            const EditorInputCapture sceneCapture{true, false, false, false, true};
            const EditorInputCapture textCapture{true, true, false, false, true};
            const EditorInputCapture unfocusedScene{false, false, false, false, true};
    require_case(dispatch_editor_key(EditorKey::F, false, false, sceneCapture) == EditorAction::FrameSelected &&
                     dispatch_editor_key(EditorKey::F, true, false, sceneCapture) == EditorAction::FrameAll &&
                     !dispatch_editor_key(EditorKey::F, false, false, textCapture),
                 "Scene shortcuts must be ignored while another field captures text input");
    require_case(dispatch_editor_key(EditorKey::Q, false, false, sceneCapture) == EditorAction::SelectHandTool &&
                     dispatch_editor_key(EditorKey::W, false, false, sceneCapture) == EditorAction::SelectMoveTool &&
                     dispatch_editor_key(EditorKey::E, false, false, sceneCapture) == EditorAction::SelectRotateTool &&
                     dispatch_editor_key(EditorKey::R, false, false, sceneCapture) == EditorAction::SelectScaleTool &&
                     !dispatch_editor_key(EditorKey::Q, false, false, textCapture) &&
                     !dispatch_editor_key(EditorKey::W, false, false, textCapture) &&
                     !dispatch_editor_key(EditorKey::E, false, false, unfocusedScene),
                 "Q/W/E/R must select labeled Scene tools only when Scene owns uncaptured input");

    const EditorInputCapture hierarchyCapture{true, false, false, false, false, true, true, false};
    require_case(dispatch_editor_key(EditorKey::F, false, false, hierarchyCapture) == EditorAction::FrameSelected &&
                     dispatch_editor_key(EditorKey::F, true, false, hierarchyCapture) == EditorAction::FrameAll &&
                     dispatch_editor_key(EditorKey::Q, false, false, hierarchyCapture) == EditorAction::SelectHandTool &&
                     dispatch_editor_key(EditorKey::W, false, false, hierarchyCapture) == EditorAction::SelectMoveTool &&
                     dispatch_editor_key(EditorKey::E, false, false, hierarchyCapture) == EditorAction::SelectRotateTool &&
                     dispatch_editor_key(EditorKey::R, false, false, hierarchyCapture) == EditorAction::SelectScaleTool &&
                     dispatch_editor_key(EditorKey::T, false, false, hierarchyCapture) == EditorAction::SelectRectangleTool,
                 "Scene shortcuts (F/Shift+F/Q/W/E/R/T) must be allowed when Hierarchy is focused");
    require_case(editor_action_allowed(EditorAction::SelectHandTool, hierarchyCapture) &&
                     editor_action_allowed(EditorAction::SelectMoveTool, hierarchyCapture) &&
                     editor_action_allowed(EditorAction::SelectRotateTool, hierarchyCapture) &&
                     editor_action_allowed(EditorAction::SelectScaleTool, hierarchyCapture) &&
                     editor_action_allowed(EditorAction::SelectRectangleTool, hierarchyCapture) &&
                     editor_action_allowed(EditorAction::FrameSelected, hierarchyCapture) &&
                     editor_action_allowed(EditorAction::FrameAll, hierarchyCapture),
                 "editor actions for Scene navigation and tool selection must be allowed when Hierarchy is focused");

    const EditorInputCapture embeddedCapture{true, false, false, false, true, false, true, true};
    require_case(!dispatch_editor_key(EditorKey::Q, false, false, embeddedCapture) &&
                     !dispatch_editor_key(EditorKey::W, false, false, embeddedCapture) &&
                     !dispatch_editor_key(EditorKey::E, false, false, embeddedCapture) &&
                     !dispatch_editor_key(EditorKey::R, false, false, embeddedCapture) &&
                     !dispatch_editor_key(EditorKey::F, false, false, embeddedCapture) &&
                     !editor_action_allowed(EditorAction::SelectMoveTool, embeddedCapture),
                 "single-key editor shortcuts must be suppressed when embedded game is focused");

    HierarchyPanel testHierarchy;
    bool testFrameSelectedFired = false;
    testHierarchy.set_on_frame_selected([&]() { testFrameSelectedFired = true; });
    SceneViewportPanel toolPanel;
    toolPanel.set_tool(SceneTool::Hand);
    require_case(toolPanel.tool() == SceneTool::Hand,
                 "hand tool must be an explicit Scene tool mode");
    toolPanel.set_tool(SceneTool::Rotate);
    require_case(toolPanel.tool() == SceneTool::Rotate,
                 "the viewport must retain one selected transform mode at a time");
    toolPanel.set_tool(SceneTool::Rectangle);
    require_case(toolPanel.tool() == SceneTool::Rectangle,
                 "rectangle sizing must be an explicit Scene tool mode");
    DocumentSession firstViewDocument = make_default_document();
    firstViewDocument.documentGeneration = 81;
    firstViewDocument.projectGeneration = 14;
    firstViewDocument.tabId = TabId{3};
    Selection sceneSelection;
    sceneSelection.objectId = 0;
    sceneSelection.selectedObjectIds = {0};
    sceneSelection.focusedPanel = FocusedPanel::Scene;
    SnapPreferences sceneSnap;
    sceneSnap.position = 0.5f;
    sceneSnap.rotationDegrees = 30.0f;
    sceneSnap.scale = 0.25f;
    sceneSnap.coordinateMode = "local";
    auto sceneNodes = toolPanel.semantic_nodes(firstViewDocument, sceneSelection, sceneSnap,
                                                "main/panel/scene");
    const auto sceneNode = [&](std::string_view id) -> const SemanticNode * {
                const auto found = std::find_if(sceneNodes.begin(), sceneNodes.end(), [&](const SemanticNode &node)
                                                { return node.id == id; });
                return found == sceneNodes.end() ? nullptr : &*found;
    };
    const auto *sceneCanvas = sceneNode("main/panel/scene/canvas");
    const auto *sceneTool = sceneNode("main/panel/scene/tool/rectangle");
    const auto *frameSelected = sceneNode("main/panel/scene/frame-selected");
    const auto *snapCoordinates = sceneNode("main/panel/scene/snap/coordinates");
    const auto *zoomNode = sceneNode("main/panel/scene/zoom");
    require_case(zoomNode != nullptr && zoomNode->role == SemanticRole::Text,
                 "Scene semantics must report a single zoom indicator");
    const size_t zoomCount = std::count_if(sceneNodes.begin(), sceneNodes.end(),
        [](const SemanticNode &n) { return n.id.find("zoom") != std::string::npos; });
    require_case(zoomCount == 1, "Scene panel must expose exactly one zoom indicator");
    require_case(sceneCanvas != nullptr && sceneCanvas->role == SemanticRole::Group &&
                     sceneTool != nullptr && sceneTool->selected &&
                     frameSelected != nullptr && frameSelected->enabled &&
                     snapCoordinates != nullptr && snapCoordinates->value == "Local",
                 "Scene semantics must report the canvas, active tool, current selection and snap settings");
    SemanticSnapshot sceneSnapshot{HostLayoutId{1}, WindowHostGeneration{1}, 1,
                                   std::move(sceneNodes), {}};
    AccessibilitySnapshots sceneSnapshots;
    require_case(sceneSnapshots.publish(std::move(sceneSnapshot)),
                 "Scene semantic nodes must satisfy the shared accessibility snapshot contract");
    require_case(toolPanel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                     "main/panel/scene/tool/hand", "activate", {}},
                     firstViewDocument, sceneSelection) && toolPanel.tool() == SceneTool::Hand,
                 "an accessible Scene tool action must select the Hand tool");
    require_case(toolPanel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                     "main/panel/scene/tool/scale", "activate", {}},
                     firstViewDocument, sceneSelection) && toolPanel.tool() == SceneTool::Scale,
                 "an accessible Scene tool action must select the corresponding authoring tool");
    require_case(!toolPanel.dispatch_semantic_action({HostLayoutId{1}, WindowHostGeneration{1}, 1,
                     "main/panel/scene/frame-selected", "activate", {}},
                     firstViewDocument, Selection{}),
                 "Frame Selected must reject a stale action after its selection disappears");
    SceneViewportPanel viewPanel;
    viewPanel.restore_view_state(firstViewDocument, 123.0f, -45.0f, 1.75f);
    const auto storedView = viewPanel.view_state();
    require_case(viewPanel.view_matches_document(firstViewDocument) &&
                     storedView.center.x == 123.0f && storedView.center.y == -45.0f &&
                     storedView.zoom == 1.75f,
                 "a restored viewport must retain its independent per-tab center and zoom");
    DocumentSession otherViewDocument = firstViewDocument;
    otherViewDocument.tabId = TabId{4};
    require_case(!viewPanel.view_matches_document(otherViewDocument),
                 "equal document generations on different tabs must not share Scene navigation state");
    require_case(snap_scene_value(3.6f, 1.0f) == 4.0f && snap_scene_value(-3.6f, 1.0f) == -4.0f &&
                     snap_scene_value(3.6f, 0.0f) == 3.6f,
                 "configured snapping must round positive and negative authored values predictably");
    require_case(resized_rectangle_dimension(20.0f, 5.0f, 2.0f) == 25.0f &&
                     resized_rectangle_dimension(8.0f, 5.0f, 2.0f, 1.0f) == 10.5f &&
                     resized_rectangle_dimension(10.0f, -20.0f, 1.0f) == 0.01f,
                 "rectangle edge drags must account for object scale, radius handles and minimum size");
    const auto resizedOffset = rectangle_offset_after_edge_resize({4.0f, -2.0f}, {0.0f, 1.0f},
                                                                  90.0f, 20.0f, 30.0f, 2.0f);
    require_case(std::abs(resizedOffset.x - 14.0f) < 0.001f &&
                     std::abs(resizedOffset.y + 2.0f) < 0.001f &&
                     std::isfinite(resized_rectangle_dimension(100.0f, 1.0e30f, 0.0001f)),
                 "rotated collider edge resizing must preserve the opposite edge in local offsets and stay finite");
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
                                Json::array({sprite(13), component(14, 3, Json{{"colliderType", 1}, {"width", 20.0}, {"height", 12.0}})}),
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
            const auto overlapping = viewport_pick_candidates(transform, preview.ordered_authoring_ids(), preview,
                                                              {420.0f, 290.0f}, 0.0f); // image center maps to world (0, 0)
            require_case(overlapping.size() == 3 && overlapping[0] == 20 && overlapping[1] == 12 &&
                             overlapping[2] == 0,
                         "overlapping targets must cycle in stable draw order, including camera outlines and missing assets");
            ViewportPickCycle cycle;
            require_case(cycle.choose(overlapping, {420.0f, 290.0f}) == 20 &&
                             cycle.choose(overlapping, {421.0f, 290.0f}) == 12 &&
                             cycle.choose(overlapping, {420.0f, 290.0f}) == 0,
                         "repeated overlapping clicks must cycle deterministically through the draw stack");
            const std::vector<ViewportPickCycle::Target> expectedTargets{
                {20, 21}, {12, 14}, {12, 13}, {0, 1}};
            const auto targets = viewport_pick_targets(transform, preview.ordered_authoring_ids(), preview,
                                                       {420.0f, 290.0f}, 0.0f);
            require_case(targets == expectedTargets,
                         "overlapping sprite, collider and camera representations must retain component identities in draw order");
            ViewportPickCycle targetCycle;
            require_case(targetCycle.choose(targets, {420.0f, 290.0f}) == expectedTargets[0] &&
                             targetCycle.choose(targets, {421.0f, 290.0f}) == expectedTargets[1] &&
                             targetCycle.choose(targets, {420.0f, 290.0f}) == expectedTargets[2] &&
                             targetCycle.choose(targets, {420.0f, 290.0f}) == expectedTargets[3],
                         "component selection cycling must make every overlapping authoring target reachable");
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
            commands.subscribe([&](const DocumentChange &, const std::vector<int> &)
                               { ++notifications; });
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

            const auto beforeRectangleHistory = commands.history_status().undoCommands;
            auto rectangleGesture = commands.begin_batch_gesture(document, {address_for_document(document, 12, 13, std::nullopt, {std::string("width")})});
            require_case(rectangleGesture.has_value(), "rectangle sizing must edit the selected Sprite component identity");
            const float resizedWidth = resized_rectangle_dimension(40.0f, 5.0f, 1.0f);
            require_case(static_cast<bool>(rectangleGesture->update(commands, document, {resizedWidth})) &&
                             rectangleGesture->commit() &&
                             document.root[1]["children"][0]["components"][0]["width"] == resizedWidth &&
                             commands.history_status().undoCommands == beforeRectangleHistory + 1,
                         "a completed visual rectangle resize must commit one component-width history entry");
            SelectionModel rectangleSelection;
            require_case(static_cast<bool>(commands.undo(document, rectangleSelection)) &&
                             document.root[1]["children"][0]["components"][0]["width"] == 40.0,
                         "undo must restore the exact initial rectangle size");

            document.root[1]["children"][0]["components"][1]["offset"] = Json{{"x", 4.0}, {"y", -2.0}};
            const Json originalCollider = document.root[1]["children"][0]["components"][1];
            const auto widthAddress = address_for_document(document, 12, 14, std::nullopt,
                                                           {std::string("width")});
            const auto offsetXAddress = address_for_document(document, 12, 14, std::nullopt,
                                                             {std::string("offset"), std::string("x")});
            const auto offsetYAddress = address_for_document(document, 12, 14, std::nullopt,
                                                             {std::string("offset"), std::string("y")});
            const auto historyBeforeCollider = commands.history_status().undoCommands;
            auto colliderGesture = commands.begin_batch_gesture(document,
                                                                {widthAddress, offsetXAddress, offsetYAddress});
            require_case(colliderGesture.has_value(), "collider edge sizing must start a three-property batch gesture");
            const std::size_t notificationsBeforeBatch = notifications;
            const auto rejectedBatchUpdate = colliderGesture->update(commands, document, {99.0});
            require_case(!rejectedBatchUpdate &&
                             document.root[1]["children"][0]["components"][1] == originalCollider,
                         "a rejected rectangle batch must leave authored geometry and offsets untouched");
            require_case(static_cast<bool>(colliderGesture->update(commands, document, {30.0, 14.0, -2.0})) &&
                             notifications == notificationsBeforeBatch + 1 &&
                             commands.history_status().undoCommands == historyBeforeCollider,
                         "collider width and local offset must preview atomically without interim history entries");
            require_case(static_cast<bool>(colliderGesture->cancel(commands, document)) &&
                             document.root[1]["children"][0]["components"][1] == originalCollider &&
                             commands.history_status().undoCommands == historyBeforeCollider,
                         "cancelling a collider resize must restore size and offset as one exact batch");
            auto committedColliderGesture = commands.begin_batch_gesture(document,
                                                                         {widthAddress, offsetXAddress, offsetYAddress});
            require_case(committedColliderGesture.has_value() &&
                             static_cast<bool>(committedColliderGesture->update(commands, document,
                                                                                {30.0, 14.0, -2.0})) &&
                             committedColliderGesture->commit() &&
                             commands.history_status().undoCommands == historyBeforeCollider + 1,
                         "a completed collider edge resize must produce one reversible history batch");
            require_case(static_cast<bool>(commands.undo(document, rectangleSelection)) &&
                             document.root[1]["children"][0]["components"][1] == originalCollider,
                         "undo must restore collider dimensions and offsets together");

            DocumentSession groupDocument;
            groupDocument.root = Json::array({object(50, 10.0f, 20.0f, Json::array(),
                                                     Json::array({object(51, 4.0f, 6.0f, Json::array(), Json::array(), 0.75f)}), 0.25f),
                                              object(60, 100.0f, 200.0f, Json::array(), Json::array(), -0.5f)});
            ++groupDocument.structureRevision;
            rebuild_document_indexes(groupDocument);
            SelectionModel groupSelection;
            groupSelection.set_objects(groupDocument, {50, 51, 60}, 60, 50);
            const auto groupRoots = selected_top_level_roots(groupDocument, groupSelection.current());
            require_case(groupRoots == std::vector<int>({50, 60}),
                         "group transforms must normalize selected descendants to top-level roots");
            const Json originalGroup = groupDocument.root;
            EditCommandService groupCommands;
            const Selection groupState = groupSelection.current();
            auto groupGesture = groupCommands.begin_batch_gesture(groupDocument,
                                                                  {address_for_document(groupDocument, 50, std::nullopt, std::nullopt,
                                                                                        {std::string("position")}),
                                                                   address_for_document(groupDocument, 60, std::nullopt, std::nullopt,
                                                                                        {std::string("position")})},
                                                                  {groupState, groupState});
            require_case(groupGesture.has_value(), "a normalized group move must begin as one batch gesture");
            const auto moved = groupGesture->update(groupCommands, groupDocument,
                                                    {Json{{"x", 15.0}, {"y", 17.0}}, Json{{"x", 105.0}, {"y", 197.0}}});
            require_case(static_cast<bool>(moved) && groupGesture->commit() &&
                             groupDocument.root[0]["position"] == Json{{"x", 15.0}, {"y", 17.0}} &&
                             groupDocument.root[1]["position"] == Json{{"x", 105.0}, {"y", 197.0}} &&
                             groupDocument.root[0]["children"][0]["position"] == Json{{"x", 4.0}, {"y", 6.0}} &&
                             groupDocument.root[0]["rotation"] == 0.25 &&
                             groupDocument.root[0]["children"][0]["rotation"] == 0.75 &&
                             groupCommands.history_status().undoCommands == 1,
                         "group move must apply one shared delta to top-level roots and preserve descendant local pose");
            require_case(static_cast<bool>(groupCommands.undo(groupDocument, groupSelection)) &&
                             groupDocument.root == originalGroup && groupSelection.current() == groupState &&
                             static_cast<bool>(groupCommands.redo(groupDocument, groupSelection)) &&
                             groupDocument.root[0]["position"] == Json{{"x", 15.0}, {"y", 17.0}} &&
                             groupDocument.root[1]["position"] == Json{{"x", 105.0}, {"y", 197.0}},
                         "group move Undo and Redo must restore the exact root block in one history step");

            groupDocument.root[0]["scale"] = Json{{"x", 1.0}, {"y", 1.0}};
            groupDocument.root[1]["scale"] = Json{{"x", 2.0}, {"y", 1.5}};
            groupDocument.root[0]["components"].push_back(component(401, 3,
                                                                    Json{{"width", 20.0}, {"height", 12.0}, {"offset", Json{{"x", 1.0}, {"y", -2.0}}}}));
            groupDocument.root[1]["components"].push_back(component(402, 3,
                                                                    Json{{"width", 40.0}, {"height", 18.0}, {"offset", Json{{"x", -3.0}, {"y", 4.0}}}}));
            ++groupDocument.structureRevision;
            rebuild_document_indexes(groupDocument);
            const Json originalGroupRectangles = groupDocument.root;
            const ViewportPoint edgeDelta{3.0f, -2.0f};
            std::vector<Json> rectangleValues;
            std::vector<PropertyAddress> rectangleAddresses;
            const std::vector<int> rectangleComponents{401, 402};
            const std::vector<int> rectangleObjects{50, 60};
            for (std::size_t index = 0; index < rectangleObjects.size(); ++index)
            {
                const Json &objectRecord = groupDocument.root[index];
                const Json &colliderRecord = objectRecord["components"].back();
                const float size = colliderRecord["width"].get<float>();
                const float scale = std::abs(objectRecord["scale"]["x"].get<float>());
                const float rotation = objectRecord["rotation"].get<float>();
                const float rotationDegrees = rotation * 180.0f / std::numbers::pi_v<float>;
                const ViewportPoint axis{std::cos(rotation), std::sin(rotation)};
                const float signedDelta = edgeDelta.x * axis.x + edgeDelta.y * axis.y;
                const float dimension = resized_rectangle_dimension(size, signedDelta, scale);
                const ViewportPoint oldOffset{colliderRecord["offset"]["x"].get<float>(),
                                              colliderRecord["offset"]["y"].get<float>()};
                const ViewportPoint offset = rectangle_offset_after_edge_resize(
                    oldOffset, axis, rotationDegrees, size, dimension, scale);
                rectangleAddresses.push_back(address_for_document(groupDocument, rectangleObjects[index],
                                                                  rectangleComponents[index], std::nullopt, {std::string("width")}));
                rectangleAddresses.push_back(address_for_document(groupDocument, rectangleObjects[index],
                                                                  rectangleComponents[index], std::nullopt,
                                                                  {std::string("offset"), std::string("x")}));
                rectangleAddresses.push_back(address_for_document(groupDocument, rectangleObjects[index],
                                                                  rectangleComponents[index], std::nullopt,
                                                                  {std::string("offset"), std::string("y")}));
                rectangleValues.emplace_back(dimension);
                rectangleValues.emplace_back(offset.x);
                rectangleValues.emplace_back(offset.y);
            }
            auto rectangleBatch = groupCommands.begin_batch_gesture(
                groupDocument, rectangleAddresses, {groupState, groupState});
            require_case(rectangleBatch.has_value() &&
                             static_cast<bool>(rectangleBatch->update(groupCommands, groupDocument,
                                                                      rectangleValues)) &&
                             rectangleBatch->commit() &&
                             groupCommands.history_status().undoCommands == 2,
                         "a multi-root rectangle resize must update matched component sizes and offsets atomically");
            require_case(static_cast<bool>(groupCommands.undo(groupDocument, groupSelection)) &&
                             groupDocument.root == originalGroupRectangles &&
                             groupSelection.current() == groupState,
                         "group rectangle Undo must restore exact dimensions, offsets, local pose, and selection");
            preview.release();
        }

        void run_unity_ergonomics_viewport_tests()
        {
            RuntimeOptions options;
            options.windowSettings.width = 320;
            options.windowSettings.height = 240;
            options.assetRoot = std::filesystem::path(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR) / "assets";
            platformator::Runtime runtime(options);

            DocumentSession document = make_default_document(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR,
                                                             std::filesystem::path(PLATFORMATOR_EDITOR_TEST_SOURCE_DIR) / "assets", 19);
            document.root = Json::array({object(1, 100.0f, 100.0f, Json::array({component(10, 3, Json{{"colliderType", 1}, {"width", 40.0}, {"height", 40.0}})})),
                                         object(2, 500.0f, 500.0f, Json::array({component(20, 3, Json{{"colliderType", 1}, {"width", 40.0}, {"height", 40.0}})}))});
            document.structureRevision++;
            rebuild_document_indexes(document);

            EditorPreview preview(runtime);
            preview.reconcile(document, true);

            SelectionModel selection;
            EditCommandService commands;
            SnapPreferences snap;
            SceneViewportPanel panel;

            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {800.0f, 600.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.Fonts->AddFontDefault();
            io.Fonts->Build();
            io.ConfigInputTrickleEventQueue = false;

            std::vector<int> hiddenList;
            std::vector<int> lockedList;
            const auto render_frame = [&]()
            {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({800.0f, 600.0f}, ImGuiCond_Always);
                ImGui::SetNextWindowFocus();
                panel.draw(document, preview, selection, commands, nullptr, nullptr, snap, false, true, &hiddenList, &lockedList);
                ImGui::Render();
            };

            render_frame();

            // 1. Hand tool left-click drag panning
            panel.set_tool(SceneTool::Hand);
            panel.restore_view_state(document, 0.0f, 0.0f, 1.0f);
            const auto initialCenter = panel.view_state().center;

            io.AddMousePosEvent(400.0f, 300.0f);
            io.AddMouseButtonEvent(0, true);
            render_frame();

            io.AddMousePosEvent(450.0f, 320.0f);
            io.AddMouseButtonEvent(0, true);
            render_frame();
            const auto pannedCenter = panel.view_state().center;
            require_case(pannedCenter.x != initialCenter.x || pannedCenter.y != initialCenter.y,
                         "hand tool left-click drag must pan viewport camera");

            io.AddMouseButtonEvent(0, false);
            render_frame();
            require_case(!panel.has_active_input(), "releasing mouse button must end hand tool panning");

            // 1b. Alt+Left-Mouse camera panning and hand cursor feedback
            panel.set_tool(SceneTool::Move);
            panel.restore_view_state(document, 0.0f, 0.0f, 1.0f);
            io.AddKeyEvent(ImGuiMod_Alt, true);
            io.AddMousePosEvent(400.0f, 300.0f);
            render_frame();
            require_case(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand,
                         "holding Alt over canvas must set mouse cursor to Hand");

            io.AddMouseButtonEvent(0, true);
            render_frame();
            require_case(panel.has_active_input(), "Alt+Left click must activate panning");
            require_case(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand,
                         "Alt+Left drag must maintain Hand cursor");

            io.AddMousePosEvent(450.0f, 320.0f);
            render_frame();
            const auto altPannedCenter = panel.view_state().center;
            require_case(altPannedCenter.x != 0.0f || altPannedCenter.y != 0.0f,
                         "Alt+Left drag must pan viewport camera");

            io.AddMouseButtonEvent(0, false);
            io.AddKeyEvent(ImGuiMod_Alt, false);
            render_frame();
            require_case(!panel.has_active_input(), "releasing Alt/mouse must end panning");

            // 1c. Cursor-centered mouse wheel zooming
            panel.restore_view_state(document, 0.0f, 0.0f, 1.0f);
            io.AddMousePosEvent(600.0f, 400.0f);
            render_frame();
            io.AddMouseWheelEvent(0.0f, 1.0f);
            render_frame();
            const auto zoomCentered = panel.view_state().center;
            require_case(zoomCentered.x != 0.0f || zoomCentered.y != 0.0f,
                         "cursor-centered zoom must offset camera center towards mouse position");
            require_case(panel.view_state().zoom > 1.0f,
                         "positive wheel delta must increase zoom");

            // 2. Frame Selected: select Object 1 and request frame selected
            selection.set(document, Selection{1, std::nullopt, std::nullopt, FocusedPanel::Scene});
            panel.request_frame_selected();
            render_frame();
            const auto framedSelectedCenter = panel.view_state().center;
            require_case(std::abs(framedSelectedCenter.x - 100.0f) < 5.0f &&
                             std::abs(framedSelectedCenter.y - 100.0f) < 5.0f,
                         "request_frame_selected must center camera on selected object");

            // 3. Frame Selected with empty selection fallback to scene bounds
            selection.set(document, Selection{});
            panel.request_frame_selected();
            render_frame();
            const auto fallbackCenter = panel.view_state().center;
            require_case(std::abs(fallbackCenter.x - 300.0f) < 50.0f &&
                             std::abs(fallbackCenter.y - 300.0f) < 50.0f,
                         "request_frame_selected with empty selection must fall back to whole scene bounds");

            // 4. Frame All (Shift+F)
            selection.set(document, Selection{1, std::nullopt, std::nullopt, FocusedPanel::Scene});
            panel.request_frame_all();
            render_frame();
            const auto frameAllCenter = panel.view_state().center;
            require_case(std::abs(frameAllCenter.x - 300.0f) < 50.0f &&
                             std::abs(frameAllCenter.y - 300.0f) < 50.0f,
                         "request_frame_all must frame entire scene bounds even with an object selected");

            // 5. Marquee selection: dragging on empty space in Move tool selects intersecting objects
            panel.set_tool(SceneTool::Move);
            panel.restore_view_state(document, 300.0f, 300.0f, 1.0f);
            selection.set(document, Selection{});
            render_frame();

            io.AddMousePosEvent(400.0f, 240.0f);
            io.AddMouseButtonEvent(0, true);
            render_frame();
            require_case(panel.marquee_state().active, "left click on empty canvas space in Move tool must activate marquee");

            io.AddMousePosEvent(100.0f, 100.0f);
            render_frame();
            require_case(panel.marquee_state().active, "marquee must remain active during mouse drag");

            io.AddMouseButtonEvent(0, false);
            render_frame();
            require_case(!panel.marquee_state().active, "releasing mouse button must end marquee");
            const auto selectedAfterMarquee = selection.current().selectedObjectIds;
            require_case(std::find(selectedAfterMarquee.begin(), selectedAfterMarquee.end(), 1) != selectedAfterMarquee.end() &&
                             std::find(selectedAfterMarquee.begin(), selectedAfterMarquee.end(), 2) == selectedAfterMarquee.end(),
                         "marquee must select intersecting object 1 and exclude object 2");

            // 6. Marquee selection excluding hidden objects
            hiddenList = {1};
            selection.set(document, Selection{});
            io.AddMousePosEvent(400.0f, 240.0f);
            io.AddMouseButtonEvent(0, true);
            render_frame();
            io.AddMousePosEvent(100.0f, 100.0f);
            render_frame();
            io.AddMouseButtonEvent(0, false);
            render_frame();
            require_case(std::find(selection.current().selectedObjectIds.begin(), selection.current().selectedObjectIds.end(), 1) == selection.current().selectedObjectIds.end(),
                         "marquee selection must exclude hidden objects");
            hiddenList.clear();

            // 7. Asset drop instantiation on Scene canvas
            const bool droppedSprite = instantiate_asset_drop(document, commands, selection, "sprites/coin.png",
                                                             EditorAssetKind::Image, 150.0f, 250.0f, std::nullopt);
            require_case(droppedSprite, "instantiate_asset_drop on scene canvas must succeed");
            require_case(selection.current().objectId.has_value(), "asset drop must select instantiated object");
            const auto newObjLoc = document.indexes.objectsById.find(*selection.current().objectId);
            require_case(newObjLoc != document.indexes.objectsById.end(), "instantiated object must exist in document indexes");

            preview.release();
            ImGui::DestroyContext();
        }
    } // namespace

    void run_viewport_cases()
    {
        require_case(viewport_pickable_ids({4, 2, 1, 3}, {2}, {3}) == std::vector<int>({4, 1}),
                     "editor hidden and locked preferences must exclude Scene picking without changing draw order");
        transform_cases();
        picking_and_gesture_cases();
        run_unity_ergonomics_viewport_tests();
    }
} // namespace platformator_editor
