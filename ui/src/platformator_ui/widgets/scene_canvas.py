from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path

import orjson
from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QWidget

from platformator_ui.scene.models import (
    AnimatorComponent,
    BoxColliderComponent,
    CameraComponent,
    CircleColliderComponent,
    FlipMode,
    GameObjectModel,
    SceneDocumentModel,
    SpriteComponent,
)
from platformator_ui.services.asset_paths import normalize_scene_editor_asset_path
from platformator_ui.services.project_paths import ProjectPaths


@dataclass(frozen=True)
class AnimationFramePreview:
    texture_path: str
    source_rect: QRectF | None


@dataclass(frozen=True)
class AnimationClipPreview:
    frames: tuple[AnimationFramePreview, ...]
    width: float
    height: float


@dataclass(frozen=True)
class SpritePreview:
    pixmap: QPixmap
    source_rect: QRectF | None
    width: float
    height: float
    flip: FlipMode


class SceneCanvasWidget(QWidget):
    objectSelected = Signal(object)
    sceneChanged = Signal()
    objectMoveFinished = Signal(int)

    def __init__(self, project_paths: ProjectPaths, parent=None) -> None:
        super().__init__(parent)
        self._project_paths = project_paths
        self._scene_document: SceneDocumentModel | None = None
        self._scene_path: Path | None = None
        self._selected_object_id: int | None = None
        self._drag_object_id: int | None = None
        self._drag_start_position = QPointF()
        self._drag_offset = QPointF()
        self._pan_active = False
        self._pan_last_position = QPointF()
        self._view_center = QPointF(0.0, 0.0)
        self._pixels_per_unit = 1.0
        self._view_initialized = False
        self._pixmap_cache: dict[Path, QPixmap] = {}
        self._animation_cache: dict[Path, AnimationClipPreview | None] = {}

        self.setMouseTracking(True)
        self.setMinimumSize(420, 320)
        self.setAutoFillBackground(True)

    def set_scene(
        self,
        scene_document: SceneDocumentModel,
        scene_path: Path | None = None,
        *,
        reset_view: bool = False,
    ) -> None:
        self._scene_document = scene_document
        self._scene_path = scene_path
        if reset_view or not self._view_initialized:
            self._fit_view_to_scene()
        self.update()

    def set_selected_object(self, object_id: int | None) -> None:
        if self._selected_object_id == object_id:
            return
        self._selected_object_id = object_id
        self.update()

    def zoom_in(self) -> None:
        self._zoom_at(QPointF(self.width() / 2, self.height() / 2), 1.2)

    def zoom_out(self) -> None:
        self._zoom_at(QPointF(self.width() / 2, self.height() / 2), 1.0 / 1.2)

    def frame_scene(self) -> None:
        self._fit_view_to_scene()
        self.update()

    def frame_selection(self) -> None:
        if self._scene_document is None or self._selected_object_id is None:
            self.frame_scene()
            return

        selected_object = self._scene_document.find_object_by_id(self._selected_object_id)
        if selected_object is None:
            self.frame_scene()
            return

        self._fit_view_to_rect(self._object_bounds(selected_object))
        self.update()

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if not self._view_initialized and self._scene_document is not None:
            self._fit_view_to_scene()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.fillRect(self.rect(), QColor("#f5efe7"))
        self._draw_grid(painter)

        if self._scene_document is None:
            return

        objects = self._scene_document.iter_objects()
        for game_object in objects:
            self._draw_game_object(painter, game_object)

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self._pan_active = True
            self._pan_last_position = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            return

        if event.button() != Qt.MouseButton.LeftButton:
            super().mousePressEvent(event)
            return

        clicked_object = self._pick_object(event.position())
        if clicked_object is None:
            self._drag_object_id = None
            self._selected_object_id = None
            self.objectSelected.emit(None)
            self.update()
            return

        self._selected_object_id = clicked_object.id
        self._drag_object_id = clicked_object.id
        self._drag_start_position = QPointF(clicked_object.position.x, clicked_object.position.y)
        world_position = self._screen_to_world(event.position())
        self._drag_offset = QPointF(
            clicked_object.position.x - world_position.x(),
            clicked_object.position.y - world_position.y(),
        )
        self.objectSelected.emit(clicked_object.id)
        self.update()

    def mouseMoveEvent(self, event) -> None:
        if self._pan_active:
            delta = event.position() - self._pan_last_position
            self._view_center = QPointF(
                self._view_center.x() - delta.x() / self._pixels_per_unit,
                self._view_center.y() - delta.y() / self._pixels_per_unit,
            )
            self._pan_last_position = event.position()
            self._view_initialized = True
            self.update()
            return

        if self._scene_document is None or self._drag_object_id is None or not (event.buttons() & Qt.MouseButton.LeftButton):
            super().mouseMoveEvent(event)
            return

        dragged_object = self._scene_document.find_object_by_id(self._drag_object_id)
        if dragged_object is None:
            self._drag_object_id = None
            return

        world_position = self._screen_to_world(event.position())
        dragged_object.position.x = world_position.x() + self._drag_offset.x()
        dragged_object.position.y = world_position.y() + self._drag_offset.y()
        self.sceneChanged.emit()
        self.update()

    def mouseReleaseEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.MiddleButton:
            self._pan_active = False
            self.unsetCursor()
            return

        if event.button() == Qt.MouseButton.LeftButton:
            moved_object_id = self._drag_object_id
            started_at = QPointF(self._drag_start_position)
            self._drag_object_id = None
            self._drag_start_position = QPointF()
            if self._scene_document is not None and moved_object_id is not None:
                moved_object = self._scene_document.find_object_by_id(moved_object_id)
                if moved_object is not None:
                    ended_at = QPointF(moved_object.position.x, moved_object.position.y)
                    if started_at != ended_at:
                        self.objectMoveFinished.emit(moved_object_id)
        super().mouseReleaseEvent(event)

    def wheelEvent(self, event) -> None:
        if event.angleDelta().y() == 0:
            super().wheelEvent(event)
            return

        factor = 1.15 if event.angleDelta().y() > 0 else 1.0 / 1.15
        self._zoom_at(event.position(), factor)
        event.accept()

    def _draw_grid(self, painter: QPainter) -> None:
        painter.save()

        minor_pen = QPen(QColor("#ddd7cf"))
        minor_pen.setWidth(1)
        painter.setPen(minor_pen)

        grid_spacing = 64.0 * self._pixels_per_unit
        if grid_spacing >= 12.0:
            start_x = math.fmod(self.width() / 2 - self._view_center.x() * self._pixels_per_unit, grid_spacing)
            while start_x < self.width():
                painter.drawLine(QPointF(start_x, 0.0), QPointF(start_x, float(self.height())))
                start_x += grid_spacing

            start_y = math.fmod(self.height() / 2 - self._view_center.y() * self._pixels_per_unit, grid_spacing)
            while start_y < self.height():
                painter.drawLine(QPointF(0.0, start_y), QPointF(float(self.width()), start_y))
                start_y += grid_spacing

        axis_pen = QPen(QColor("#b0693f"))
        axis_pen.setWidth(2)
        painter.setPen(axis_pen)
        origin = self._world_to_screen(QPointF(0.0, 0.0))
        painter.drawLine(QPointF(origin.x(), 0.0), QPointF(origin.x(), float(self.height())))
        painter.drawLine(QPointF(0.0, origin.y()), QPointF(float(self.width()), origin.y()))
        painter.restore()

    def _draw_game_object(self, painter: QPainter, game_object: GameObjectModel) -> None:
        sprite_preview = self._resolve_sprite_preview(game_object)
        camera_component = game_object.find_component(CameraComponent)
        box_collider = game_object.find_component(BoxColliderComponent)
        circle_collider = game_object.find_component(CircleColliderComponent)

        if sprite_preview is not None:
            self._draw_sprite(painter, game_object, sprite_preview)

        if camera_component is not None:
            self._draw_camera_bounds(painter, game_object, camera_component)

        if box_collider is not None:
            self._draw_box_collider(painter, game_object, box_collider)
        if circle_collider is not None:
            self._draw_circle_collider(painter, game_object, circle_collider)

        if sprite_preview is None and camera_component is None and box_collider is None and circle_collider is None:
            self._draw_placeholder(painter, game_object)

        if game_object.id == self._selected_object_id:
            self._draw_selection_outline(painter, game_object)

    def _draw_sprite(self, painter: QPainter, game_object: GameObjectModel, preview: SpritePreview) -> None:
        position = QPointF(game_object.position.x, game_object.position.y)
        screen_position = self._world_to_screen(position)
        scale_x = max(abs(game_object.scale.x), 0.001)
        scale_y = max(abs(game_object.scale.y), 0.001)
        draw_width = preview.width * scale_x * self._pixels_per_unit
        draw_height = preview.height * scale_y * self._pixels_per_unit
        if draw_width <= 0.0 or draw_height <= 0.0:
            return

        source_rect = preview.source_rect or QRectF(0.0, 0.0, float(preview.pixmap.width()), float(preview.pixmap.height()))

        painter.save()
        painter.translate(screen_position)
        painter.rotate(math.degrees(game_object.rotation))
        if preview.flip == FlipMode.HORIZONTAL:
            painter.scale(-1.0, 1.0)
        elif preview.flip == FlipMode.VERTICAL:
            painter.scale(1.0, -1.0)

        target_rect = QRectF(-draw_width / 2, -draw_height / 2, draw_width, draw_height)
        painter.drawPixmap(target_rect, preview.pixmap, source_rect)
        painter.restore()

    def _draw_camera_bounds(self, painter: QPainter, game_object: GameObjectModel, camera_component: CameraComponent) -> None:
        world_rect = QRectF(
            game_object.position.x,
            game_object.position.y,
            camera_component.width,
            camera_component.height,
        )
        screen_rect = self._world_rect_to_screen(world_rect)
        painter.save()
        pen = QPen(QColor("#6c8c52"))
        pen.setStyle(Qt.PenStyle.DashLine)
        pen.setWidth(2)
        painter.setPen(pen)
        painter.drawRect(screen_rect)
        painter.restore()

    def _draw_box_collider(self, painter: QPainter, game_object: GameObjectModel, collider: BoxColliderComponent) -> None:
        position = self._world_to_screen(QPointF(game_object.position.x, game_object.position.y))
        width = collider.width * max(abs(game_object.scale.x), 0.001) * self._pixels_per_unit
        height = collider.height * max(abs(game_object.scale.y), 0.001) * self._pixels_per_unit

        painter.save()
        painter.translate(position)
        painter.rotate(math.degrees(game_object.rotation))
        pen = QPen(QColor("#1d6fa5"))
        pen.setWidth(2)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRect(QRectF(-width / 2, -height / 2, width, height))
        painter.restore()

    def _draw_circle_collider(self, painter: QPainter, game_object: GameObjectModel, collider: CircleColliderComponent) -> None:
        position = self._world_to_screen(QPointF(game_object.position.x, game_object.position.y))
        radius = collider.radius * max(abs(game_object.scale.x), abs(game_object.scale.y), 0.001) * self._pixels_per_unit

        painter.save()
        pen = QPen(QColor("#1d6fa5"))
        pen.setWidth(2)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(position, radius, radius)
        painter.restore()

    def _draw_placeholder(self, painter: QPainter, game_object: GameObjectModel) -> None:
        center = self._world_to_screen(QPointF(game_object.position.x, game_object.position.y))
        painter.save()
        pen = QPen(QColor("#7a6a58"))
        pen.setWidth(2)
        painter.setPen(pen)
        size = 8.0
        painter.drawLine(QPointF(center.x() - size, center.y()), QPointF(center.x() + size, center.y()))
        painter.drawLine(QPointF(center.x(), center.y() - size), QPointF(center.x(), center.y() + size))
        painter.restore()

    def _draw_selection_outline(self, painter: QPainter, game_object: GameObjectModel) -> None:
        bounds = self._object_bounds(game_object)
        screen_rect = self._world_rect_to_screen(bounds)
        painter.save()
        pen = QPen(QColor("#c94927"))
        pen.setWidth(2)
        pen.setStyle(Qt.PenStyle.DashLine)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRect(screen_rect.adjusted(-4.0, -4.0, 4.0, 4.0))
        painter.restore()

    def _pick_object(self, screen_position: QPointF) -> GameObjectModel | None:
        if self._scene_document is None:
            return None

        for game_object in reversed(self._scene_document.iter_objects()):
            if self._world_rect_to_screen(self._object_bounds(game_object)).contains(screen_position):
                return game_object
        return None

    def _object_bounds(self, game_object: GameObjectModel) -> QRectF:
        bounds: QRectF | None = None

        sprite_component = game_object.find_component(SpriteComponent)
        if sprite_component is not None:
            sprite_preview = self._resolve_sprite_preview(game_object)
            width = sprite_preview.width if sprite_preview is not None else max(sprite_component.width, 16.0)
            height = sprite_preview.height if sprite_preview is not None else max(sprite_component.height, 16.0)
            sprite_rect = QRectF(
                game_object.position.x - (width * max(abs(game_object.scale.x), 0.001)) / 2,
                game_object.position.y - (height * max(abs(game_object.scale.y), 0.001)) / 2,
                width * max(abs(game_object.scale.x), 0.001),
                height * max(abs(game_object.scale.y), 0.001),
            )
            bounds = sprite_rect if bounds is None else bounds.united(sprite_rect)

        camera_component = game_object.find_component(CameraComponent)
        if camera_component is not None:
            camera_rect = QRectF(
                game_object.position.x,
                game_object.position.y,
                camera_component.width,
                camera_component.height,
            )
            bounds = camera_rect if bounds is None else bounds.united(camera_rect)

        box_collider = game_object.find_component(BoxColliderComponent)
        if box_collider is not None:
            bounds = self._merge_rect(bounds, self._box_collider_bounds(game_object, box_collider))

        circle_collider = game_object.find_component(CircleColliderComponent)
        if circle_collider is not None:
            radius = circle_collider.radius * max(abs(game_object.scale.x), abs(game_object.scale.y), 0.001)
            circle_rect = QRectF(
                game_object.position.x - radius,
                game_object.position.y - radius,
                radius * 2.0,
                radius * 2.0,
            )
            bounds = self._merge_rect(bounds, circle_rect)

        if bounds is None:
            bounds = QRectF(game_object.position.x - 12.0, game_object.position.y - 12.0, 24.0, 24.0)
        return bounds

    def _box_collider_bounds(self, game_object: GameObjectModel, collider: BoxColliderComponent) -> QRectF:
        half_width = collider.width * max(abs(game_object.scale.x), 0.001) / 2.0
        half_height = collider.height * max(abs(game_object.scale.y), 0.001) / 2.0
        sin_rotation = math.sin(game_object.rotation)
        cos_rotation = math.cos(game_object.rotation)

        world_points: list[QPointF] = []
        for local_x, local_y in ((-half_width, -half_height), (half_width, -half_height), (half_width, half_height), (-half_width, half_height)):
            rotated_x = local_x * cos_rotation - local_y * sin_rotation
            rotated_y = local_x * sin_rotation + local_y * cos_rotation
            world_points.append(QPointF(game_object.position.x + rotated_x, game_object.position.y + rotated_y))

        min_x = min(point.x() for point in world_points)
        max_x = max(point.x() for point in world_points)
        min_y = min(point.y() for point in world_points)
        max_y = max(point.y() for point in world_points)
        return QRectF(min_x, min_y, max_x - min_x, max_y - min_y)

    def _resolve_sprite_preview(self, game_object: GameObjectModel) -> SpritePreview | None:
        sprite_component = game_object.find_component(SpriteComponent)
        if sprite_component is None:
            return None

        animator_component = game_object.find_component(AnimatorComponent)
        if animator_component is not None and animator_component.animationClipFilePath:
            clip_preview = self._load_animation_clip(animator_component.animationClipFilePath)
            if clip_preview is not None and clip_preview.frames:
                frame_index = max(0, min(animator_component.currentFrameIndex, len(clip_preview.frames) - 1))
                frame = clip_preview.frames[frame_index]
                pixmap = self._load_pixmap(frame.texture_path)
                if pixmap is not None and not pixmap.isNull():
                    width = clip_preview.width or (frame.source_rect.width() if frame.source_rect is not None else float(pixmap.width()))
                    height = clip_preview.height or (frame.source_rect.height() if frame.source_rect is not None else float(pixmap.height()))
                    return SpritePreview(pixmap, frame.source_rect, width, height, sprite_component.flip)

        if not sprite_component.textureFilePath:
            return None

        pixmap = self._load_pixmap(sprite_component.textureFilePath)
        if pixmap is None or pixmap.isNull():
            return None

        source_rect = None
        if sprite_component.sourceRectEnabled and sprite_component.sourceRect is not None:
            source_rect = QRectF(
                sprite_component.sourceRect.x,
                sprite_component.sourceRect.y,
                sprite_component.sourceRect.w,
                sprite_component.sourceRect.h,
            )

        width = sprite_component.width or (source_rect.width() if source_rect is not None else float(pixmap.width()))
        height = sprite_component.height or (source_rect.height() if source_rect is not None else float(pixmap.height()))
        return SpritePreview(pixmap, source_rect, width, height, sprite_component.flip)

    def _load_animation_clip(self, asset_path: str) -> AnimationClipPreview | None:
        resolved_path = self._resolve_asset_file(asset_path)
        if resolved_path is None:
            return None

        cache_key = resolved_path.resolve(strict=False)
        if cache_key in self._animation_cache:
            return self._animation_cache[cache_key]

        try:
            payload = orjson.loads(resolved_path.read_bytes())
        except (OSError, orjson.JSONDecodeError):
            self._animation_cache[cache_key] = None
            return None

        if not isinstance(payload, dict):
            self._animation_cache[cache_key] = None
            return None

        frames: list[AnimationFramePreview] = []
        for raw_frame in payload.get("frames", []):
            if not isinstance(raw_frame, dict):
                continue

            texture_path = raw_frame.get("textureWrapperFilePath")
            if not isinstance(texture_path, str) or not texture_path:
                continue

            source_rect = None
            if raw_frame.get("hasSourceRect") and isinstance(raw_frame.get("sourceRect"), dict):
                raw_source_rect = raw_frame["sourceRect"]
                source_rect = QRectF(
                    float(raw_source_rect.get("x", 0.0)),
                    float(raw_source_rect.get("y", 0.0)),
                    float(raw_source_rect.get("w", 0.0)),
                    float(raw_source_rect.get("h", 0.0)),
                )

            frames.append(AnimationFramePreview(texture_path=texture_path, source_rect=source_rect))

        clip_preview = AnimationClipPreview(
            frames=tuple(frames),
            width=float(payload.get("width", 0.0) or 0.0),
            height=float(payload.get("height", 0.0) or 0.0),
        )
        self._animation_cache[cache_key] = clip_preview
        return clip_preview

    def _load_pixmap(self, asset_path: str) -> QPixmap | None:
        resolved_path = self._resolve_asset_file(asset_path)
        if resolved_path is None:
            return None

        cache_key = resolved_path.resolve(strict=False)
        cached_pixmap = self._pixmap_cache.get(cache_key)
        if cached_pixmap is not None:
            return cached_pixmap

        pixmap = QPixmap(str(resolved_path))
        if pixmap.isNull():
            return None

        self._pixmap_cache[cache_key] = pixmap
        return pixmap

    def _resolve_asset_file(self, asset_path: str) -> Path | None:
        if not asset_path:
            return None

        canonical_path = normalize_scene_editor_asset_path(
            asset_path,
            scene_path=self._scene_path,
            repo_root=self._project_paths.repo_root,
        )
        if not canonical_path:
            return None

        return self._project_paths.repo_root / canonical_path

    def _fit_view_to_scene(self) -> None:
        if self._scene_document is None or self.width() <= 0 or self.height() <= 0:
            self._view_center = QPointF(0.0, 0.0)
            self._pixels_per_unit = 1.0
            return

        bounds: QRectF | None = None
        for game_object in self._scene_document.iter_objects():
            bounds = self._merge_rect(bounds, self._object_bounds(game_object))

        if bounds is None:
            self._view_center = QPointF(0.0, 0.0)
            self._pixels_per_unit = 1.0
            self._view_initialized = True
            return

        self._fit_view_to_rect(bounds)

    def _world_to_screen(self, point: QPointF) -> QPointF:
        return QPointF(
            (point.x() - self._view_center.x()) * self._pixels_per_unit + self.width() / 2,
            (point.y() - self._view_center.y()) * self._pixels_per_unit + self.height() / 2,
        )

    def _screen_to_world(self, point: QPointF) -> QPointF:
        return QPointF(
            (point.x() - self.width() / 2) / self._pixels_per_unit + self._view_center.x(),
            (point.y() - self.height() / 2) / self._pixels_per_unit + self._view_center.y(),
        )

    def _world_rect_to_screen(self, rect: QRectF) -> QRectF:
        top_left = self._world_to_screen(rect.topLeft())
        bottom_right = self._world_to_screen(rect.bottomRight())
        return QRectF(top_left, bottom_right).normalized()

    def _fit_view_to_rect(self, bounds: QRectF) -> None:
        self._view_center = bounds.center()
        padded_width = max(bounds.width() + 96.0, 128.0)
        padded_height = max(bounds.height() + 96.0, 128.0)
        scale_x = self.width() / padded_width if padded_width > 0.0 else 1.0
        scale_y = self.height() / padded_height if padded_height > 0.0 else 1.0
        self._pixels_per_unit = max(0.05, min(scale_x, scale_y, 8.0))
        self._view_initialized = True

    def _zoom_at(self, screen_point: QPointF, factor: float) -> None:
        before_world = self._screen_to_world(screen_point)
        new_scale = max(0.05, min(self._pixels_per_unit * factor, 16.0))
        if abs(new_scale - self._pixels_per_unit) <= 1e-6:
            return

        self._pixels_per_unit = new_scale
        after_world = self._screen_to_world(screen_point)
        self._view_center = QPointF(
            self._view_center.x() + before_world.x() - after_world.x(),
            self._view_center.y() + before_world.y() - after_world.y(),
        )
        self._view_initialized = True
        self.update()

    @staticmethod
    def _merge_rect(left: QRectF | None, right: QRectF) -> QRectF:
        return right if left is None else left.united(right)