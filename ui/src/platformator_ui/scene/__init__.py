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
    from .serializer import SceneSerializationError, SceneSerializer
    from .templates import create_empty_scene, create_game_object, create_main_camera_object, create_named_component
    from .validation import Severity, ValidationIssue, validate_scene_document

__all__ = [
    "AnimatorComponent",
    "AudioComponent",
    "BaseComponentModel",
    "BodyType",
    "BoxColliderComponent",
    "CameraComponent",
    "CircleColliderComponent",
    "ColliderType",
    "ComponentType",
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
    "validate_scene_document",
]

_EXPORTS = {
    "AnimatorComponent": (".models", "AnimatorComponent"),
    "AudioComponent": (".models", "AudioComponent"),
    "BaseComponentModel": (".models", "BaseComponentModel"),
    "BodyType": (".models", "BodyType"),
    "BoxColliderComponent": (".models", "BoxColliderComponent"),
    "CameraComponent": (".models", "CameraComponent"),
    "CircleColliderComponent": (".models", "CircleColliderComponent"),
    "ColliderType": (".models", "ColliderType"),
    "ComponentType": (".models", "ComponentType"),
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
