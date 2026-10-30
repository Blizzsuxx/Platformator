#pragma once

#include <optional>
#include <vector>

#include <SDL3/SDL.h>

#include "document.h"
#include "editcommands.h"
#include "preview.h"

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
    void reset() noexcept;

  private:
    std::vector<int> previousCandidates_;
    ViewportPoint previousPoint_{};
    std::size_t index_ = 0;
    bool hasPrevious_ = false;
};

std::vector<int> viewport_pick_candidates(const ViewportTransform &transform,
                                          const std::vector<int> &orderedDocumentIds,
                                          const EditorPreview &preview, ViewportPoint screenPoint,
                                          float tolerancePixels = 4.0f);
std::optional<ViewportPoint> object_local_position_for_world(const DocumentSession &document,
                                                             int objectId, ViewportPoint worldPosition);
std::optional<ViewportPoint> collider_offset_for_world(const DocumentSession &document,
                                                       int objectId, ViewportPoint colliderWorldPosition);
std::optional<PropertyGesture> begin_object_position_gesture(EditCommandService &commands,
                                                             const DocumentSession &document, int objectId,
                                                             SelectionState selection = {});
std::optional<PropertyGesture> begin_collider_offset_gesture(EditCommandService &commands,
                                                            const DocumentSession &document, int objectId,
                                                            SelectionState selection = {});

enum class SceneGestureKind
{
    Position,
    ColliderOffset,
    Scale,
    Rotation
};

class SceneViewportPanel
{
  public:
    void draw(DocumentSession &document, EditorPreview &preview, SelectionModel &selection,
              EditCommandService &commands, SDL_Window *window, SDL_Renderer *renderer,
              bool modalOrTextCapture = false, bool editingEnabled = true);
    void set_visible(bool visible) noexcept;
    bool is_visible() const noexcept;
    void adjust_zoom(float factor) noexcept;
    void request_frame_selected() noexcept;
    void request_frame_all() noexcept;
    void commit_gesture();
    void cancel_gesture(EditCommandService &commands, DocumentSession &document);
    bool gesture_active() const noexcept;
    bool has_active_input() const noexcept;

  private:
    ViewportTransform transform_;
    ViewportPoint viewCenter_{};
    float viewZoom_ = 1.0f;
    float pendingZoomFactor_ = 1.0f;
    ViewportPickCycle pickCycle_;
    std::optional<PropertyGesture> gesture_;
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
    float gestureStartAngle_ = 0.0f;
    int pendingScaleAxis_ = -1;
    int gestureScaleAxis_ = -1;
    bool pendingDrag_ = false;
    int pendingDragObjectId_ = 0;
    ViewportPoint pendingPressScreen_{};
    ViewportPoint pendingPointerWorld_{};
    ViewportPoint pendingTargetWorld_{};
    bool panning_ = false;
    ViewportPoint panStart_{};
    bool open_ = true;
    bool hasView_ = false;
    bool frameSelectedRequested_ = false;
    bool frameAllRequested_ = false;
};
} // namespace platformator_editor
