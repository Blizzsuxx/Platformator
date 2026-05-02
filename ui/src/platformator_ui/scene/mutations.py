from __future__ import annotations

from copy import deepcopy

from .ids import SceneIdAllocator
from .models import BaseComponentModel, GameObjectModel, SceneDocumentModel, ScriptBehaviorModel, ScriptComponentModel, Vector2Model
from .templates import create_game_object


def add_game_object(
    scene_document: SceneDocumentModel,
    allocator: SceneIdAllocator,
    *,
    parent_id: int | None = None,
    name: str = "Game Object",
    position: Vector2Model | None = None,
) -> GameObjectModel:
    game_object = create_game_object(allocator, name=create_unique_object_name(scene_document, name))
    if position is not None:
        game_object.position = position.model_copy(deep=True)

    if parent_id is not None:
        parent = scene_document.find_object_by_id(parent_id)
        if parent is None:
            raise ValueError(f"Parent object {parent_id} was not found.")

        if position is None:
            game_object.position = parent.position.model_copy(deep=True)
            game_object.position.x += 32.0
            game_object.position.y += 32.0
        parent.children.append(game_object)
        return game_object

    scene_document.objects.append(game_object)
    return game_object


def duplicate_game_object(
    scene_document: SceneDocumentModel,
    object_id: int,
    allocator: SceneIdAllocator,
) -> GameObjectModel | None:
    location = _find_object_location(scene_document.objects, object_id)
    if location is None:
        return None

    container, index, original, _parent = location
    duplicate = original.model_copy(deep=True)
    _reassign_object_ids(duplicate, allocator)
    duplicate.name = create_unique_object_name(scene_document, f"{original.name} Copy")
    duplicate.position.x += 32.0
    duplicate.position.y += 32.0
    container.insert(index + 1, duplicate)
    return duplicate


def remove_game_object(scene_document: SceneDocumentModel, object_id: int) -> GameObjectModel | None:
    location = _find_object_location(scene_document.objects, object_id)
    if location is None:
        return None

    container, index, game_object, _parent = location
    del container[index]
    return game_object


def find_parent_object(scene_document: SceneDocumentModel, object_id: int) -> GameObjectModel | None:
    location = _find_object_location(scene_document.objects, object_id)
    if location is None:
        return None
    return location[3]


def remove_component(game_object: GameObjectModel, component_id: int) -> BaseComponentModel | None:
    for index, component in enumerate(game_object.components):
        if component.id == component_id:
            return game_object.components.pop(index)
    return None


def add_behavior(
    script_component: ScriptComponentModel,
    behavior_type: str,
    *,
    initial_values: dict[str, object] | None = None,
) -> ScriptBehaviorModel:
    payload = {"type": behavior_type, **deepcopy(initial_values or {})}
    behavior = ScriptBehaviorModel.model_validate(payload)
    script_component.behaviors.append(behavior)
    return behavior


def create_unique_object_name(scene_document: SceneDocumentModel, base_name: str) -> str:
    normalized_base_name = base_name.strip() or "Game Object"
    existing_names = {game_object.name for game_object in scene_document.iter_objects()}
    if normalized_base_name not in existing_names:
        return normalized_base_name

    suffix = 2
    while True:
        candidate = f"{normalized_base_name} {suffix}"
        if candidate not in existing_names:
            return candidate
        suffix += 1


def _reassign_object_ids(game_object: GameObjectModel, allocator: SceneIdAllocator) -> None:
    game_object.id = allocator.allocate()
    for component in game_object.components:
        component.id = allocator.allocate()
    for child in game_object.children:
        _reassign_object_ids(child, allocator)


def _find_object_location(
    objects: list[GameObjectModel],
    object_id: int,
    *,
    parent: GameObjectModel | None = None,
) -> tuple[list[GameObjectModel], int, GameObjectModel, GameObjectModel | None] | None:
    for index, game_object in enumerate(objects):
        if game_object.id == object_id:
            return objects, index, game_object, parent

        nested_result = _find_object_location(game_object.children, object_id, parent=game_object)
        if nested_result is not None:
            return nested_result

    return None