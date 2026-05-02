from __future__ import annotations

import re
from enum import Enum
from pathlib import Path, PurePosixPath

from platformator_ui.scene.models import AnimatorComponent, AudioComponent, SceneDocumentModel, ScriptComponentModel, SpriteComponent


WINDOWS_DRIVE_PATH_RE = re.compile(r"^[a-zA-Z]:/")


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


def is_absolute_path(path: str) -> bool:
    return path.startswith("/") or WINDOWS_DRIVE_PATH_RE.match(path) is not None


def canonicalize_path(raw_path: str) -> str:
    normalized = raw_path.strip().replace("\\", "/").lower()
    if not normalized:
        return ""

    components: list[str] = []
    for component in normalized.split("/"):
        if component in {"", "."}:
            continue
        if component == "..":
            if components:
                components.pop()
            else:
                components.append(component)
            continue
        components.append(component)

    candidate = "/".join(components)
    return "" if candidate == "." else candidate


def extract_assets_relative_path(canonical_path: str) -> str:
    components = [component for component in canonical_path.split("/") if component]
    for index, component in enumerate(components):
        if component == "assets":
            return "/".join(components[index:])
    return ""


def canonicalize_asset_path(raw_path: str) -> str | None:
    canonical_path = canonicalize_path(raw_path)
    if not canonical_path:
        return ""

    asset_relative_path = extract_assets_relative_path(canonical_path)
    if not asset_relative_path:
        if is_absolute_path(canonical_path):
            return None
        asset_relative_path = canonicalize_path(f"assets/{canonical_path}")

    if asset_relative_path == "assets" or asset_relative_path.startswith("assets/"):
        return asset_relative_path
    return None


def normalize_scene_editor_asset_path(
    raw_path: str,
    *,
    scene_path: Path | None = None,
    repo_root: Path | None = None,
) -> str | None:
    canonical = canonicalize_asset_path(raw_path)
    if canonical is not None:
        return canonical

    stripped_path = raw_path.strip()
    if not stripped_path or is_absolute_path(stripped_path) or scene_path is None or repo_root is None:
        return canonical

    resolved_scene_relative_path = scene_path.parent.joinpath(PurePosixPath(stripped_path)).resolve(strict=False)
    try:
        repo_relative = resolved_scene_relative_path.relative_to(repo_root.resolve(strict=False)).as_posix()
    except ValueError:
        return None

    return canonicalize_asset_path(repo_relative)


def normalize_scene_asset_paths(
    scene_document: SceneDocumentModel,
    *,
    scene_path: Path | None = None,
    repo_root: Path | None = None,
) -> None:
    for game_object in scene_document.iter_objects():
        for component in game_object.components:
            if isinstance(component, SpriteComponent):
                component.textureFilePath = _normalize_optional_asset_path(
                    component.textureFilePath,
                    scene_path=scene_path,
                    repo_root=repo_root,
                )
            elif isinstance(component, AnimatorComponent):
                component.animationClipFilePath = _normalize_optional_asset_path(
                    component.animationClipFilePath,
                    scene_path=scene_path,
                    repo_root=repo_root,
                )
            elif isinstance(component, AudioComponent):
                component.filePath = _normalize_optional_asset_path(
                    component.filePath,
                    scene_path=scene_path,
                    repo_root=repo_root,
                )
            elif isinstance(component, ScriptComponentModel):
                for behavior in component.behaviors:
                    for field_name, asset_kind in KNOWN_BEHAVIOR_ASSET_FIELDS.items():
                        field_value = getattr(behavior, field_name, None)
                        if isinstance(field_value, str):
                            normalized_value = _normalize_optional_asset_path(
                                field_value,
                                scene_path=scene_path,
                                repo_root=repo_root,
                            )
                            if normalized_value is not None:
                                setattr(behavior, field_name, normalized_value)


def _normalize_optional_asset_path(
    raw_path: str,
    *,
    scene_path: Path | None,
    repo_root: Path | None,
) -> str:
    if not raw_path:
        return ""

    normalized = normalize_scene_editor_asset_path(raw_path, scene_path=scene_path, repo_root=repo_root)
    return normalized if normalized is not None else raw_path
