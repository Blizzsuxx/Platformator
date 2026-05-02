from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
from pathlib import Path
import re
from typing import Any

import orjson

from platformator_ui.scene.models import ComponentType, SceneDocumentModel, ScriptComponentModel


SERIALIZABLE_SCRIPT_RE = re.compile(
    r"SERIALIZABLE_SCRIPT\s*\(\s*([A-Za-z_:][A-Za-z0-9_:]*)\s*(?:,\s*([^\)]*?))?\)",
    re.MULTILINE | re.DOTALL,
)


def discover_behavior_templates(
    repo_root: Path,
    scene_document: SceneDocumentModel | None = None,
) -> dict[str, dict[str, Any]]:
    templates = deepcopy(_discover_workspace_behavior_templates(repo_root.resolve()))
    if scene_document is not None:
        _merge_behavior_samples(templates, _collect_scene_behavior_samples(scene_document))
    return dict(sorted(templates.items(), key=lambda item: item[0].casefold()))


@lru_cache(maxsize=4)
def _discover_workspace_behavior_templates(repo_root: Path) -> dict[str, dict[str, Any]]:
    declared_fields = _scan_serializable_script_fields(repo_root)
    scene_samples = _scan_scene_behavior_samples(repo_root)

    templates: dict[str, dict[str, Any]] = {}
    for behavior_name in sorted(set(declared_fields) | set(scene_samples), key=str.casefold):
        field_names = declared_fields.get(behavior_name, ())
        sample = scene_samples.get(behavior_name, {})
        template_defaults: dict[str, Any] = {}

        for field_name in field_names:
            if field_name in sample:
                template_defaults[field_name] = _neutralize_value(sample[field_name])

        for field_name, sample_value in sample.items():
            if field_name not in template_defaults:
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