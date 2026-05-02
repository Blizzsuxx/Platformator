from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from enum import Enum
from functools import lru_cache
from pathlib import Path
import re
from typing import Any

import orjson

from platformator_ui.scene.models import ComponentType, SceneDocumentModel, ScriptComponentModel
from platformator_ui.services.asset_paths import AssetKind


SERIALIZABLE_SCRIPT_RE = re.compile(
    r"SERIALIZABLE_SCRIPT\s*\(\s*([A-Za-z_:][A-Za-z0-9_:]*)\s*(?:,\s*([^\)]*?))?\)",
    re.MULTILINE | re.DOTALL,
)
ASSET_REFERENCE_DECLARATION_RE = re.compile(
    r"AssetReference<\s*([A-Za-z_:][A-Za-z0-9_:]*)\s*>\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
    re.MULTILINE,
)
ASSET_REFERENCE_ALIAS_RE = re.compile(
    r"platformator::(TextureAssetRef|AudioAssetRef|AnimationClipRef)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
    re.MULTILINE,
)
OBJECT_REFERENCE_DECLARATION_RE = re.compile(
    r"ObjectReference<\s*([A-Za-z_:][A-Za-z0-9_:]*)\s*>\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
    re.MULTILINE,
)


class ObjectReferenceTargetKind(str, Enum):
    SCENE_OBJECT = "scene_object"
    COMPONENT = "component"


@dataclass(frozen=True)
class ObjectReferenceDescriptor:
    target_kind: ObjectReferenceTargetKind
    target_type_name: str | None = None


def discover_behavior_templates(
    repo_root: Path,
    scene_document: SceneDocumentModel | None = None,
) -> dict[str, dict[str, Any]]:
    templates = deepcopy(_discover_workspace_behavior_templates(repo_root.resolve()))
    if scene_document is not None:
        _merge_behavior_samples(templates, _collect_scene_behavior_samples(scene_document))
    return dict(sorted(templates.items(), key=lambda item: item[0].casefold()))


@lru_cache(maxsize=4)
def discover_behavior_asset_fields(repo_root: Path) -> dict[str, dict[str, AssetKind]]:
    asset_fields, _ = _scan_serializable_script_reference_fields(repo_root.resolve())
    return asset_fields


@lru_cache(maxsize=4)
def discover_behavior_object_reference_fields(repo_root: Path) -> dict[str, dict[str, ObjectReferenceDescriptor]]:
    _, object_fields = _scan_serializable_script_reference_fields(repo_root.resolve())
    return object_fields


@lru_cache(maxsize=4)
def _discover_workspace_behavior_templates(repo_root: Path) -> dict[str, dict[str, Any]]:
    declared_fields = _scan_serializable_script_fields(repo_root)
    scene_samples = _scan_scene_behavior_samples(repo_root)
    _, object_reference_fields = _scan_serializable_script_reference_fields(repo_root)

    templates: dict[str, dict[str, Any]] = {}
    for behavior_name in sorted(set(declared_fields) | set(scene_samples), key=str.casefold):
        field_names = declared_fields.get(behavior_name, ())
        sample = scene_samples.get(behavior_name, {})
        reference_fields = object_reference_fields.get(behavior_name, {})
        template_defaults: dict[str, Any] = {}

        for field_name in field_names:
            if field_name in reference_fields:
                template_defaults[field_name] = None
                continue
            if field_name in sample:
                template_defaults[field_name] = _neutralize_value(sample[field_name])

        for field_name, sample_value in sample.items():
            if field_name not in template_defaults:
                if field_name in reference_fields:
                    template_defaults[field_name] = None
                    continue
                template_defaults[field_name] = _neutralize_value(sample_value)

        templates[behavior_name] = template_defaults

    return templates


def _scan_serializable_script_fields(repo_root: Path) -> dict[str, tuple[str, ...]]:
    discovered: dict[str, tuple[str, ...]] = {}
    for file_path in _iter_source_files(repo_root):
        try:
            content = file_path.read_text(encoding="utf-8")
        except OSError:
            continue

        for match in SERIALIZABLE_SCRIPT_RE.finditer(content):
            behavior_name = match.group(1).strip()
            field_block = match.group(2) or ""
            field_names = tuple(
                field_name.strip()
                for field_name in field_block.replace("\n", " ").split(",")
                if field_name.strip()
            )
            discovered[behavior_name] = field_names

    return discovered


def _scan_scene_behavior_samples(repo_root: Path) -> dict[str, dict[str, Any]]:
    samples: dict[str, dict[str, Any]] = {}
    for file_path in _iter_scene_files(repo_root):
        try:
            payload = orjson.loads(file_path.read_bytes())
        except (OSError, orjson.JSONDecodeError):
            continue

        if not isinstance(payload, list):
            continue

        for raw_object in payload:
            _collect_raw_object_behavior_samples(raw_object, samples)

    return samples


def _scan_serializable_script_reference_fields(
    repo_root: Path,
) -> tuple[dict[str, dict[str, AssetKind]], dict[str, dict[str, ObjectReferenceDescriptor]]]:
    asset_fields_by_behavior: dict[str, dict[str, AssetKind]] = {}
    object_fields_by_behavior: dict[str, dict[str, ObjectReferenceDescriptor]] = {}

    for file_path in _iter_source_files(repo_root):
        try:
            content = file_path.read_text(encoding="utf-8")
        except OSError:
            continue

        asset_declarations = _scan_asset_reference_declarations(content)
        object_declarations = _scan_object_reference_declarations(content)

        for match in SERIALIZABLE_SCRIPT_RE.finditer(content):
            behavior_name = match.group(1).strip()
            field_block = match.group(2) or ""
            field_names = tuple(
                field_name.strip()
                for field_name in field_block.replace("\n", " ").split(",")
                if field_name.strip()
            )
            if not field_names:
                continue

            asset_fields = asset_fields_by_behavior.setdefault(behavior_name, {})
            object_fields = object_fields_by_behavior.setdefault(behavior_name, {})

            for field_name in field_names:
                asset_kind = asset_declarations.get(field_name)
                if asset_kind is not None:
                    asset_fields[field_name] = asset_kind

                object_descriptor = object_declarations.get(field_name)
                if object_descriptor is not None:
                    object_fields[field_name] = object_descriptor

            if not asset_fields:
                asset_fields_by_behavior.pop(behavior_name, None)
            if not object_fields:
                object_fields_by_behavior.pop(behavior_name, None)

    return asset_fields_by_behavior, object_fields_by_behavior


def _scan_asset_reference_declarations(content: str) -> dict[str, AssetKind]:
    declarations: dict[str, AssetKind] = {}

    for match in ASSET_REFERENCE_DECLARATION_RE.finditer(content):
        declarations[match.group(2)] = _asset_kind_for_type(match.group(1))

    for match in ASSET_REFERENCE_ALIAS_RE.finditer(content):
        declarations[match.group(2)] = _asset_kind_for_alias(match.group(1))

    return declarations


def _scan_object_reference_declarations(content: str) -> dict[str, ObjectReferenceDescriptor]:
    declarations: dict[str, ObjectReferenceDescriptor] = {}

    for match in OBJECT_REFERENCE_DECLARATION_RE.finditer(content):
        declarations[match.group(2)] = _object_reference_descriptor_for_type(match.group(1))

    return declarations


def _asset_kind_for_type(type_name: str) -> AssetKind:
    short_name = type_name.rsplit("::", maxsplit=1)[-1]
    if short_name == "TextureWrapper":
        return AssetKind.TEXTURE
    if short_name == "AudioWrapper":
        return AssetKind.AUDIO
    if short_name == "AnimationClip":
        return AssetKind.ANIMATION_CLIP
    return AssetKind.GENERIC


def _asset_kind_for_alias(alias_name: str) -> AssetKind:
    if alias_name == "TextureAssetRef":
        return AssetKind.TEXTURE
    if alias_name == "AudioAssetRef":
        return AssetKind.AUDIO
    if alias_name == "AnimationClipRef":
        return AssetKind.ANIMATION_CLIP
    return AssetKind.GENERIC


def _object_reference_descriptor_for_type(type_name: str) -> ObjectReferenceDescriptor:
    short_name = type_name.rsplit("::", maxsplit=1)[-1]
    if short_name == "GameObject":
        return ObjectReferenceDescriptor(ObjectReferenceTargetKind.SCENE_OBJECT, short_name)
    return ObjectReferenceDescriptor(ObjectReferenceTargetKind.COMPONENT, short_name)


def _collect_raw_object_behavior_samples(raw_object: Any, samples: dict[str, dict[str, Any]]) -> None:
    if not isinstance(raw_object, dict):
        return

    for raw_component in raw_object.get("components", []):
        if not isinstance(raw_component, dict) or raw_component.get("type") != int(ComponentType.SCRIPT):
            continue

        for raw_behavior in raw_component.get("behaviors", []):
            if not isinstance(raw_behavior, dict):
                continue

            behavior_name = raw_behavior.get("type")
            if not isinstance(behavior_name, str) or not behavior_name:
                continue

            samples.setdefault(
                behavior_name,
                {key: value for key, value in raw_behavior.items() if key != "type"},
            )

    for child in raw_object.get("children", []):
        _collect_raw_object_behavior_samples(child, samples)


def _collect_scene_behavior_samples(scene_document: SceneDocumentModel) -> dict[str, dict[str, Any]]:
    samples: dict[str, dict[str, Any]] = {}
    for game_object in scene_document.iter_objects():
        for component in game_object.components:
            if not isinstance(component, ScriptComponentModel):
                continue

            for behavior in component.behaviors:
                if behavior.type not in samples:
                    payload = behavior.model_dump(mode="python", exclude_none=False)
                    samples[behavior.type] = {key: value for key, value in payload.items() if key != "type"}

    return samples


def _merge_behavior_samples(
    templates: dict[str, dict[str, Any]],
    samples: dict[str, dict[str, Any]],
) -> None:
    for behavior_name, sample in samples.items():
        template = templates.setdefault(behavior_name, {})
        for field_name, sample_value in sample.items():
            template.setdefault(field_name, _neutralize_value(sample_value))


def _neutralize_value(value: Any) -> Any:
    if isinstance(value, bool):
        return False
    if isinstance(value, int):
        return 0
    if isinstance(value, float):
        return 0.0
    if isinstance(value, str):
        return ""
    if isinstance(value, list):
        return []
    if isinstance(value, dict):
        return {key: _neutralize_value(nested_value) for key, nested_value in value.items()}
    return None


def _iter_source_files(repo_root: Path) -> list[Path]:
    source_files: list[Path] = []
    for relative_root in ("src", "tests", "examples"):
        base_dir = repo_root / relative_root
        if not base_dir.exists():
            continue

        for pattern in ("*.h", "*.hpp", "*.cpp", "*.cc", "*.cxx"):
            source_files.extend(base_dir.rglob(pattern))

    return source_files


def _iter_scene_files(repo_root: Path) -> list[Path]:
    scene_files: list[Path] = []
    for relative_root in ("assets", "examples"):
        base_dir = repo_root / relative_root
        if base_dir.exists():
            scene_files.extend(base_dir.rglob("*.scene"))
    return scene_files