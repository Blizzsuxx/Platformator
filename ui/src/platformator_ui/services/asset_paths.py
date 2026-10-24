from __future__ import annotations

from enum import Enum
from pathlib import Path, PurePosixPath

from platformator_ui.scene.models import AnimatorComponent, AudioComponent, SceneDocumentModel, ScriptComponentModel, SpriteComponent


class AssetKind(str, Enum):
    TEXTURE = "texture"
    AUDIO = "audio"
    ANIMATION_CLIP = "animation_clip"
    MODEL = "model"
    SCRIPT = "script"
    GENERIC = "generic"


KNOWN_BEHAVIOR_ASSET_FIELDS: dict[str, AssetKind] = {
    "icon": AssetKind.TEXTURE,
    "idleClip": AssetKind.ANIMATION_CLIP,
    "sound": AssetKind.AUDIO,
}


def canonicalize_asset_path(raw_path: str) -> str | None:
    normalized = raw_path.replace("\\", "/")
    if not normalized:
        return ""
    path = PurePosixPath(normalized)
    if (
        not normalized.startswith("assets/")
        or len(path.parts) < 2
        or ".." in path.parts
        or normalized.endswith(("/", "/."))
    ):
        return None
    return path.as_posix()


def normalize_scene_asset_paths(
    scene_document: SceneDocumentModel,
    *,
    repo_root: Path | None = None,
) -> None:
    behavior_asset_fields: dict[str, dict[str, AssetKind]] = {}
    if repo_root is not None:
        from platformator_ui.services.behavior_catalog import discover_behavior_asset_fields

        behavior_asset_fields = discover_behavior_asset_fields(repo_root.resolve(strict=False))

    for game_object in scene_document.iter_objects():
        for component in game_object.components:
            if isinstance(component, SpriteComponent):
                component.textureFilePath = _normalize_optional_asset_path(component.textureFilePath)
            elif isinstance(component, AnimatorComponent):
                component.animationClipFilePath = _normalize_optional_asset_path(component.animationClipFilePath)
            elif isinstance(component, AudioComponent):
                component.filePath = _normalize_optional_asset_path(component.filePath)
            elif isinstance(component, ScriptComponentModel):
                for behavior in component.behaviors:
                    asset_field_names = behavior_asset_fields.get(behavior.type, {}).keys() or KNOWN_BEHAVIOR_ASSET_FIELDS.keys()
                    for field_name in asset_field_names:
                        field_value = getattr(behavior, field_name, None)
                        if isinstance(field_value, str):
                            setattr(behavior, field_name, _normalize_optional_asset_path(field_value))


def _normalize_optional_asset_path(raw_path: str) -> str:
    normalized = canonicalize_asset_path(raw_path)
    return normalized if normalized is not None else raw_path
