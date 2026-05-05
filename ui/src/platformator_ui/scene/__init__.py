from __future__ import annotations

from importlib import import_module
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .ids import SceneIdAllocator
    from .models import (
        AnimatorComponent,
        AudioComponent,
        BaseComponentModel,
        BodyType,
        BoxColliderComponent,
        CameraComponent,
        CircleColliderComponent,
        ColliderType,
        ComponentType,
        GameObjectModel,
        RectModel,
        SceneDocumentModel,
        ScriptBehaviorModel,
        ScriptComponentModel,
        SpriteComponent,
        UnknownComponentModel,
        Vector2Model,
    )
    from .mutations import add_behavior, add_game_object, create_unique_object_name, duplicate_game_object, find_parent_object, remove_component, remove_game_object, synchronize_behavior_fields
    from .serializer import SceneSerializationError, SceneSerializer
    from .templates import create_empty_scene, create_game_object, create_main_camera_object, create_named_component
    from .validation import Severity, ValidationIssue, validate_scene_document

__all__ = [
    "AnimatorComponent",
    "AudioComponent",
    "BaseComponentModel",
    "BodyType",
    "add_behavior",
    "add_game_object",
    "BoxColliderComponent",
    "CameraComponent",
    "CircleColliderComponent",
    "ColliderType",
    "ComponentType",
    "create_unique_object_name",
    "GameObjectModel",
    "RectModel",
    "SceneDocumentModel",
    "SceneIdAllocator",
    "SceneSerializationError",
    "SceneSerializer",
    "ScriptBehaviorModel",
    "ScriptComponentModel",
    "Severity",
    "SpriteComponent",
    "UnknownComponentModel",
    "ValidationIssue",
    "Vector2Model",
    "create_empty_scene",
    "create_game_object",
    "create_main_camera_object",
    "create_named_component",
    "duplicate_game_object",
    "find_parent_object",
    "remove_component",
    "remove_game_object",
    "synchronize_behavior_fields",
    "validate_scene_document",
]

_EXPORTS = {
    "AnimatorComponent": (".models", "AnimatorComponent"),
    "AudioComponent": (".models", "AudioComponent"),
    "BaseComponentModel": (".models", "BaseComponentModel"),
    "BodyType": (".models", "BodyType"),
    "add_behavior": (".mutations", "add_behavior"),
    "add_game_object": (".mutations", "add_game_object"),
    "BoxColliderComponent": (".models", "BoxColliderComponent"),
    "CameraComponent": (".models", "CameraComponent"),
    "CircleColliderComponent": (".models", "CircleColliderComponent"),
    "ColliderType": (".models", "ColliderType"),
    "ComponentType": (".models", "ComponentType"),
    "create_unique_object_name": (".mutations", "create_unique_object_name"),
    "GameObjectModel": (".models", "GameObjectModel"),
    "RectModel": (".models", "RectModel"),
    "SceneDocumentModel": (".models", "SceneDocumentModel"),
    "SceneIdAllocator": (".ids", "SceneIdAllocator"),
    "SceneSerializationError": (".serializer", "SceneSerializationError"),
    "SceneSerializer": (".serializer", "SceneSerializer"),
    "ScriptBehaviorModel": (".models", "ScriptBehaviorModel"),
    "ScriptComponentModel": (".models", "ScriptComponentModel"),
    "Severity": (".validation", "Severity"),
    "SpriteComponent": (".models", "SpriteComponent"),
    "UnknownComponentModel": (".models", "UnknownComponentModel"),
    "ValidationIssue": (".validation", "ValidationIssue"),
    "Vector2Model": (".models", "Vector2Model"),
    "create_empty_scene": (".templates", "create_empty_scene"),
    "create_game_object": (".templates", "create_game_object"),
    "create_main_camera_object": (".templates", "create_main_camera_object"),
    "create_named_component": (".templates", "create_named_component"),
    "duplicate_game_object": (".mutations", "duplicate_game_object"),
    "find_parent_object": (".mutations", "find_parent_object"),
    "remove_component": (".mutations", "remove_component"),
    "remove_game_object": (".mutations", "remove_game_object"),
    "synchronize_behavior_fields": (".mutations", "synchronize_behavior_fields"),
    "validate_scene_document": (".validation", "validate_scene_document"),
}


def __getattr__(name: str) -> object:
    if name not in _EXPORTS:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

    module_name, attribute_name = _EXPORTS[name]
    module = import_module(module_name, __name__)
    value = getattr(module, attribute_name)
    globals()[name] = value
    return value
