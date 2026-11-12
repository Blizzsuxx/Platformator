#pragma once

#include <optional>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "accessibility.h"
#include "document.h"
#include "editcommands.h"
#include "preview.h"
#include "preferences.h"
#include "selection.h"

namespace platformator_editor
{
struct ViewportPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

struct ViewportRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class ViewportTransform
{
  public:
    bool configure(ViewportRect imageRect, float framebufferScaleX, float framebufferScaleY,
                   float baseWorldWidth, float baseWorldHeight,
                   float centerX, float centerY, float zoom = 1.0f);
    bool valid() const noexcept;
    const ViewportRect &image_rect() const noexcept;
    const ViewportRect &content_rect() const noexcept;
    int target_width() const noexcept;
    int target_height() const noexcept;
    float camera_left() const noexcept;
    float camera_top() const noexcept;
    float world_width() const noexcept;
    float world_height() const noexcept;
    float zoom() const noexcept;
    std::optional<ViewportPoint> screen_to_world(ViewportPoint point) const noexcept;
    ViewportPoint world_to_screen(ViewportPoint point) const noexcept;
    void pan(float logicalDeltaX, float logicalDeltaY) noexcept;
    bool zoom(float factor);
    bool zoom_at(ViewportPoint screenPoint, float factor);
    bool frame_bounds(ViewportRect worldBounds, float padding = 1.1f);

  private:
    void update_projection() noexcept;

    ViewportRect imageRect_{};
    ViewportRect contentRect_{};
    float framebufferScaleX_ = 1.0f;
    float framebufferScaleY_ = 1.0f;
    float baseWorldWidth_ = 0.0f;
    float baseWorldHeight_ = 0.0f;
    float centerX_ = 0.0f;
    float centerY_ = 0.0f;
    float zoom_ = 1.0f;
    float cameraLeft_ = 0.0f;
    float cameraTop_ = 0.0f;
    float worldWidth_ = 0.0f;
    float worldHeight_ = 0.0f;
    int targetWidth_ = 0;
    int targetHeight_ = 0;
    bool valid_ = false;
};

class ViewportPickCycle
{
  public:
    std::optional<int> choose(const std::vector<int> &candidates, ViewportPoint screenPoint);
    struct Target
    {
        int objectId = 0;
        std::optional<int> componentId;

        bool operator==(const Target &) const = default;
    };
    std::optional<Target> choose(const std::vector<Target> &candidates, ViewportPoint screenPoint);
    void reset() noexcept;

  private:
    std::vector<int> previousCandidates_;
    std::vector<Target> previousTargets_;
    ViewportPoint previousPoint_{};
    std::size_t index_ = 0;
    bool hasPrevious_ = false;
};

std::vector<int> viewport_pick_candidates(const ViewportTransform &transform,
                                          const std::vector<int> &orderedDocumentIds,
                                          const EditorPreview &preview, ViewportPoint screenPoint,
                                          float tolerancePixels = 4.0f);
std::vector<int> viewport_pickable_ids(const std::vector<int> &orderedDocumentIds,
                                       const std::vector<int> &hiddenObjectIds,
                                       const std::vector<int> &lockedObjectIds);
std::vector<ViewportPickCycle::Target> viewport_pick_targets(
    const ViewportTransform &transform, const std::vector<int> &orderedDocumentIds,
    const EditorPreview &preview, ViewportPoint screenPoint, float tolerancePixels = 4.0f);
std::optional<ViewportPoint> object_local_position_for_world(const DocumentSession &document,
                                                             int objectId, ViewportPoint worldPosition);
std::optional<ViewportPoint> collider_offset_for_world(const DocumentSession &document,
                                                       int objectId, ViewportPoint colliderWorldPosition);
std::optional<PropertyGesture> begin_object_position_gesture(EditCommandService &commands,
                                                             DocumentSession &document, int objectId,
                                                             SelectionState selection = {});
std::optional<PropertyGesture> begin_collider_offset_gesture(EditCommandService &commands,
                                                            DocumentSession &document, int objectId,
                                                            SelectionState selection = {});

enum class SceneGestureKind
{
    Position,
    ColliderOffset,
    Scale,
    Rotation,
    RectangleWidth,
    RectangleHeight
};

enum class SceneTool
{
    Hand,
    Move,
    Rotate,
    Scale,
    Rectangle
};

float snap_scene_value(float value, float increment) noexcept;
float resized_rectangle_dimension(float initial, float signedEdgeDeltaWorld,
                                  float absoluteScale, float edgeMultiplier = 2.0f) noexcept;
ViewportPoint rectangle_offset_after_edge_resize(ViewportPoint initialOffset,
                                                 ViewportPoint outwardAxisWorld,
                                                 float rotationDegrees,
                                                 float initialDimension,
                                                 float resizedDimension,
                                                 float absoluteScale) noexcept;

struct MarqueeSelectionState
{
    bool active = false;
    bool additive = false;
    ViewportPoint startScreen{};
    ViewportPoint currentScreen{};
};

class SceneViewportPanel
{
  public:
    struct ViewState
    {
        ViewportPoint center{};
        float zoom = 1.0f;
    };

    void draw(DocumentSession &document, EditorPreview &preview, SelectionModel &selection,
              EditCommandService &commands, SDL_Window *window, SDL_Renderer *renderer,
              SnapPreferences &snapPreferences, bool modalOrTextCapture = false,
              bool editingEnabled = true, std::vector<int> *hiddenObjectIds = nullptr,
              std::vector<int> *lockedObjectIds = nullptr,
              std::function<SDL_Texture *(SDL_Texture *, int, int)> imageResolver = {});
    void set_visible(bool visible) noexcept;
    bool is_visible() const noexcept;
    void adjust_zoom(float factor) noexcept;
    void request_frame_selected() noexcept;
    void request_frame_all() noexcept;
    void set_tool(SceneTool tool) noexcept;
    SceneTool tool() const noexcept;
    const MarqueeSelectionState &marquee_state() const noexcept { return marquee_; }
    void reset_view_state(const DocumentSession &document) noexcept;
    void restore_view_state(const DocumentSession &document, float centerX, float centerY,
                            float zoom) noexcept;
    ViewState view_state() const noexcept;
    bool view_matches_document(const DocumentSession &document) const noexcept;
    void commit_gesture();
    void cancel_gesture(EditCommandService &commands, DocumentSession &document);
    bool gesture_active() const noexcept;
    bool has_active_input() const noexcept;
    std::vector<SemanticNode> semantic_nodes(const DocumentSession &document,
                                              const Selection &selection,
                                              const SnapPreferences &snapPreferences,
                                              std::string_view parentId) const;
    bool dispatch_semantic_action(const SemanticAction &action,
                                  const DocumentSession &document,
                                  const Selection &selection);

  private:
    ViewportTransform transform_;
    ViewportPoint viewCenter_{};
    float viewZoom_ = 1.0f;
    float pendingZoomFactor_ = 1.0f;
    ViewportPickCycle pickCycle_;
    std::optional<PropertyGesture> gesture_;
    std::optional<BatchPropertyGesture> batchGesture_;
    std::vector<int> gestureObjectIds_;
    std::vector<ViewportPoint> gestureStartWorldPositions_;
    std::vector<ViewportPoint> gestureStartScales_;
    std::vector<float> gestureStartRotations_;
    bool groupTransformGesture_ = false;
    std::vector<float> gestureRectangleStartSizes_;
    std::vector<float> gestureRectangleScales_;
    std::vector<float> gestureRectangleRotations_;
    std::vector<ViewportPoint> gestureRectangleOffsets_;
    std::vector<ViewportPoint> gestureRectangleAxes_;
    bool groupRectangleGesture_ = false;
    PropertyAddress gestureAddress_{};
    SceneGestureKind pendingGestureKind_ = SceneGestureKind::Position;
    SceneGestureKind gestureKind_ = SceneGestureKind::Position;
    ViewportPoint gesturePointerWorld_{};
    ViewportPoint gestureStartWorld_{};
    ViewportPoint pendingAxisWorld_{};
    ViewportPoint pendingAxisScreen_{};
    ViewportPoint gestureAxisWorld_{};
    ViewportPoint gestureAxisScreen_{};
    ViewportPoint pendingPivotScreen_{};
    ViewportPoint gesturePivotScreen_{};
    ViewportPoint gestureStartScale_{1.0f, 1.0f};
    float gestureStartRotation_ = 0.0f;
    float gestureStartRectangleSize_ = 0.0f;
    float pendingStartRectangleSize_ = 0.0f;
    ViewportPoint pendingRectangleOffset_{};
    ViewportPoint gestureStartRectangleOffset_{};
    float pendingRectangleScale_ = 1.0f;
    float pendingRectangleMultiplier_ = 2.0f;
    float gestureRectangleMultiplier_ = 2.0f;
    std::optional<int> pendingComponentId_;
    std::string pendingProperty_;
    float gestureStartAngle_ = 0.0f;
    int pendingScaleAxis_ = -1;
    int gestureScaleAxis_ = -1;
    bool pendingDrag_ = false;
    bool pendingRectangleKeepsOppositeEdge_ = false;
    bool gestureRectangleKeepsOppositeEdge_ = false;
    int pendingDragObjectId_ = 0;
    ViewportPoint pendingPressScreen_{};
    ViewportPoint pendingPointerWorld_{};
    ViewportPoint pendingTargetWorld_{};
    bool panning_ = false;
    ViewportPoint panStart_{};
    bool open_ = true;
    bool hasView_ = false;
    std::uint64_t viewDocumentGeneration_ = 0;
    std::uint64_t viewProjectGeneration_ = 0;
    TabId viewTabId_;
    bool frameSelectedRequested_ = false;
    bool frameAllRequested_ = false;
    SceneTool tool_ = SceneTool::Move;
    MarqueeSelectionState marquee_{};
    bool showGrid_ = true;
};
} // namespace platformator_editor
