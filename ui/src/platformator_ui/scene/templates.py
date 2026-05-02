from __future__ import annotations

from .ids import SceneIdAllocator
from .models import (
    AnimatorComponent,
    AudioComponent,
    BodyType,
    BoxColliderComponent,
    CameraComponent,
    CircleColliderComponent,
    ComponentType,
    GameObjectModel,
    RigidbodyComponent,
    SceneDocumentModel,
    ScriptComponentModel,
    SpriteComponent,
    Vector2Model,
)


def create_game_object(allocator: SceneIdAllocator, name: str = "Game Object") -> GameObjectModel:
    return GameObjectModel(
        id=allocator.allocate(),
        name=name,
        active=True,
        rotation=0.0,
        position=Vector2Model(x=0.0, y=0.0),
        scale=Vector2Model(x=1.0, y=1.0),
        tag="",
        children=[],
        components=[],
    )


def create_main_camera_object(allocator: SceneIdAllocator) -> GameObjectModel:
    camera_object = create_game_object(allocator, name="Main Camera")
    camera_object.components.append(
        CameraComponent(
            id=allocator.allocate(),
            width=640.0,
            height=480.0,
        )
    )
    return camera_object


def create_named_component(component_name: str, allocator: SceneIdAllocator):
    component_name = component_name.lower()
    component_id = allocator.allocate()

    if component_name == "camera":
        return CameraComponent(id=component_id, width=640.0, height=480.0)
    if component_name == "rigidbody":
        return RigidbodyComponent(id=component_id, bodyType=BodyType.DYNAMIC, gravity=True)
    if component_name == "boxcollider":
        return BoxColliderComponent(id=component_id, width=32.0, height=32.0)
    if component_name == "circlecollider":
        return CircleColliderComponent(id=component_id, radius=16.0)
    if component_name == "sprite":
        return SpriteComponent(id=component_id, width=32.0, height=32.0)
    if component_name == "animator":
        return AnimatorComponent(id=component_id)
    if component_name == "audio":
        return AudioComponent(id=component_id)
    if component_name == "script":
        return ScriptComponentModel(id=component_id)

    raise ValueError(f"Unsupported component template '{component_name}'.")


def create_empty_scene(include_default_camera: bool = True) -> SceneDocumentModel:
    scene = SceneDocumentModel(objects=[])
    if include_default_camera:
        allocator = SceneIdAllocator.from_scene(scene)
        scene.objects.append(create_main_camera_object(allocator))
    return scene
