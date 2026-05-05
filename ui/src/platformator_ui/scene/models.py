from __future__ import annotations

from enum import IntEnum
from typing import Any, Literal, TypeVar

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


class ComponentType(IntEnum):
    ANIMATOR = 0
    AUDIO = 1
    CAMERA = 2
    COLLIDER = 3
    RIGID_BODY = 4
    SCRIPT = 5
    SPRITE = 6


class ColliderType(IntEnum):
    CIRCLE = 0
    BOX = 1


class BodyType(IntEnum):
    DYNAMIC = 0
    STATIC = 1
    KINEMATIC = 2


class FlipMode(IntEnum):
    NONE = 0
    HORIZONTAL = 1
    VERTICAL = 2


class SceneModel(BaseModel):
    model_config = ConfigDict(extra="allow", validate_assignment=True)


class Vector2Model(SceneModel):
    x: float = 0.0
    y: float = 0.0


class RectModel(SceneModel):
    x: float = 0.0
    y: float = 0.0
    w: float = 0.0
    h: float = 0.0


class ScriptBehaviorModel(SceneModel):
    type: str


class BaseComponentModel(SceneModel):
    id: int = Field(default=0, ge=0)
    type: int

    @property
    def component_label(self) -> str:
        try:
            return ComponentType(self.type).name.replace("_", " ").title()
        except ValueError:
            return f"Unknown ({self.type})"


class BoxColliderComponent(BaseComponentModel):
    type: Literal[ComponentType.COLLIDER] = ComponentType.COLLIDER
    colliderType: Literal[ColliderType.BOX] = ColliderType.BOX
    width: float = 32.0
    height: float = 32.0
    offset: Vector2Model = Field(default_factory=Vector2Model)
    trigger: bool = False
    collisionGroup: int = 1
    collisionMask: int = 1

    @property
    def component_label(self) -> str:
        return "Box Collider"


class CircleColliderComponent(BaseComponentModel):
    type: Literal[ComponentType.COLLIDER] = ComponentType.COLLIDER
    colliderType: Literal[ColliderType.CIRCLE] = ColliderType.CIRCLE
    radius: float = 16.0
    offset: Vector2Model = Field(default_factory=Vector2Model)
    trigger: bool = False
    collisionGroup: int = 1
    collisionMask: int = 1

    @property
    def component_label(self) -> str:
        return "Circle Collider"


class RigidbodyComponent(BaseComponentModel):
    type: Literal[ComponentType.RIGID_BODY] = ComponentType.RIGID_BODY
    velocity: Vector2Model = Field(default_factory=Vector2Model)
    force: Vector2Model = Field(default_factory=Vector2Model)
    mass: float = 1.0
    angularVelocity: float = 0.0
    torque: float = 0.0
    friction: float = 1.0
    restitution: float = 0.0
    bodyType: BodyType = BodyType.DYNAMIC
    gravity: bool = True


class SpriteComponent(BaseComponentModel):
    type: Literal[ComponentType.SPRITE] = ComponentType.SPRITE
    textureFilePath: str = ""
    flip: FlipMode = FlipMode.NONE
    width: float = 32.0
    height: float = 32.0
    sourceRectEnabled: bool = False
    sourceRect: RectModel | None = None


class AnimatorComponent(BaseComponentModel):
    type: Literal[ComponentType.ANIMATOR] = ComponentType.ANIMATOR
    currentFrameIndex: int = 0
    playbackSpeed: float = 1.0
    playing: bool = False
    animationClipFilePath: str = ""


class CameraComponent(BaseComponentModel):
    type: Literal[ComponentType.CAMERA] = ComponentType.CAMERA
    width: float = 640.0
    height: float = 480.0

    @model_validator(mode="before")
    @classmethod
    def migrate_legacy_camera_rect(cls, value: Any) -> Any:
        if not isinstance(value, dict):
            return value

        migrated = dict(value)
        raw_camera = migrated.pop("camera", None)
        if isinstance(raw_camera, dict):
            migrated.setdefault("width", raw_camera.get("w", 640.0))
            migrated.setdefault("height", raw_camera.get("h", 480.0))
        return migrated


class AudioComponent(BaseComponentModel):
    type: Literal[ComponentType.AUDIO] = ComponentType.AUDIO
    filePath: str = ""
    gain: float = 1.0
    loopCount: int = 0
    autoPlay: bool = False


class ScriptComponentModel(BaseComponentModel):
    type: Literal[ComponentType.SCRIPT] = ComponentType.SCRIPT
    behaviors: list[ScriptBehaviorModel] = Field(default_factory=list)

    @field_validator("behaviors", mode="before")
    @classmethod
    def parse_behaviors(cls, value: Any) -> list[ScriptBehaviorModel]:
        if value is None:
            return []
        return [behavior if isinstance(behavior, ScriptBehaviorModel) else ScriptBehaviorModel.model_validate(behavior) for behavior in value]


class UnknownComponentModel(BaseComponentModel):
    pass


SceneComponentModel = (
    BoxColliderComponent
    | CircleColliderComponent
    | RigidbodyComponent
    | SpriteComponent
    | AnimatorComponent
    | CameraComponent
    | AudioComponent
    | ScriptComponentModel
    | UnknownComponentModel
)

TComponent = TypeVar("TComponent", bound=BaseComponentModel)


def parse_component(value: Any) -> SceneComponentModel:
    if isinstance(value, BaseComponentModel):
        return value

    if not isinstance(value, dict):
        raise TypeError(f"Scene component must be an object, got {type(value)!r}")

    component_type = value.get("type")
    collider_type = value.get("colliderType")

    if component_type == ComponentType.COLLIDER:
        if collider_type == ColliderType.BOX:
            return BoxColliderComponent.model_validate(value)
        if collider_type == ColliderType.CIRCLE:
            return CircleColliderComponent.model_validate(value)
        return UnknownComponentModel.model_validate(value)

    model_by_type: dict[int, type[BaseComponentModel]] = {
        ComponentType.RIGID_BODY: RigidbodyComponent,
        ComponentType.SPRITE: SpriteComponent,
        ComponentType.ANIMATOR: AnimatorComponent,
        ComponentType.CAMERA: CameraComponent,
        ComponentType.AUDIO: AudioComponent,
        ComponentType.SCRIPT: ScriptComponentModel,
    }
    model_type = model_by_type.get(component_type, UnknownComponentModel)
    return model_type.model_validate(value)


class GameObjectModel(SceneModel):
    id: int = Field(default=0, ge=0)
    rotation: float = 0.0
    active: bool = True
    position: Vector2Model = Field(default_factory=Vector2Model)
    scale: Vector2Model = Field(default_factory=lambda: Vector2Model(x=1.0, y=1.0))
    name: str = "Game Object"
    tag: str = ""
    children: list[GameObjectModel] = Field(default_factory=list)
    components: list[SceneComponentModel] = Field(default_factory=list)

    @field_validator("components", mode="before")
    @classmethod
    def parse_components(cls, value: Any) -> list[SceneComponentModel]:
        if value is None:
            return []
        return [parse_component(component) for component in value]

    def find_component(self, component_type: type[TComponent]) -> TComponent | None:
        for component in self.components:
            if isinstance(component, component_type):
                return component
        return None

    def find_component_by_kind(self, component_kind: ComponentType) -> BaseComponentModel | None:
        for component in self.components:
            if component.type == component_kind:
                return component
        return None

    def find_component_by_id(self, component_id: int) -> BaseComponentModel | None:
        for component in self.components:
            if component.id == component_id:
                return component
        return None

    def iter_children_recursive(self) -> list[GameObjectModel]:
        descendants: list[GameObjectModel] = []
        for child in self.children:
            descendants.append(child)
            descendants.extend(child.iter_children_recursive())
        return descendants


GameObjectModel.model_rebuild()


class SceneDocumentModel(SceneModel):
    objects: list[GameObjectModel] = Field(default_factory=list)
    source_path: str | None = Field(default=None, exclude=True, repr=False)

    def iter_objects(self) -> list[GameObjectModel]:
        items: list[GameObjectModel] = []
        for game_object in self.objects:
            items.append(game_object)
            items.extend(game_object.iter_children_recursive())
        return items

    def find_object_by_id(self, object_id: int) -> GameObjectModel | None:
        for game_object in self.iter_objects():
            if game_object.id == object_id:
                return game_object
        return None

    def find_object_by_name(self, name: str) -> GameObjectModel | None:
        for game_object in self.iter_objects():
            if game_object.name == name:
                return game_object
        return None

    def all_component_ids(self) -> set[int]:
        component_ids: set[int] = set()
        for game_object in self.iter_objects():
            component_ids.update(component.id for component in game_object.components)
        return component_ids
