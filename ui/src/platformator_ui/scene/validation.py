from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from platformator_ui.services.asset_paths import KNOWN_BEHAVIOR_ASSET_FIELDS, canonicalize_asset_path

from .models import AnimatorComponent, AudioComponent, SceneDocumentModel, ScriptComponentModel, SpriteComponent


class Severity(str, Enum):
    ERROR = "error"
    WARNING = "warning"


@dataclass(frozen=True)
class ValidationIssue:
    severity: Severity
    location: str
    message: str


def validate_scene_document(scene_document: SceneDocumentModel) -> list[ValidationIssue]:
    issues: list[ValidationIssue] = []
    seen_ids: dict[int, str] = {}
    object_ids = {game_object.id for game_object in scene_document.iter_objects()}
    component_ids: set[int] = set()

    for game_object in scene_document.iter_objects():
        _track_duplicate_id(seen_ids, game_object.id, f"object:{game_object.name}", issues)
        for component in game_object.components:
            component_location = f"{game_object.name}/{component.component_label}#{component.id}"
            _track_duplicate_id(seen_ids, component.id, component_location, issues)
            component_ids.add(component.id)
            issues.extend(_validate_component_assets(game_object.name, component))

            if isinstance(component, ScriptComponentModel):
                for behavior in component.behaviors:
                    for field_name, value in behavior.model_dump(mode="python").items():
                        if field_name == "type":
                            continue
                        if field_name == "targetObject" and isinstance(value, int) and value not in object_ids:
                            issues.append(
                                ValidationIssue(
                                    severity=Severity.ERROR,
                                    location=f"{game_object.name}/Script/{behavior.type}.{field_name}",
                                    message=f"Unknown object id reference {value}.",
                                )
                            )
                        if field_name == "emitter" and isinstance(value, int) and value not in component_ids:
                            issues.append(
                                ValidationIssue(
                                    severity=Severity.ERROR,
                                    location=f"{game_object.name}/Script/{behavior.type}.{field_name}",
                                    message=f"Unknown component id reference {value}.",
                                )
                            )
                        if field_name in KNOWN_BEHAVIOR_ASSET_FIELDS and isinstance(value, str) and value:
                            issues.extend(
                                _validate_asset_path(
                                    f"{game_object.name}/Script/{behavior.type}.{field_name}",
                                    value,
                                )
                            )

    return issues


def _track_duplicate_id(
    seen_ids: dict[int, str],
    candidate_id: int,
    location: str,
    issues: list[ValidationIssue],
) -> None:
    previous_location = seen_ids.get(candidate_id)
    if previous_location is not None:
        issues.append(
            ValidationIssue(
                severity=Severity.ERROR,
                location=location,
                message=f"Duplicate id {candidate_id}; already used at {previous_location}.",
            )
        )
        return
    seen_ids[candidate_id] = location


def _validate_component_assets(game_object_name: str, component) -> list[ValidationIssue]:
    if isinstance(component, SpriteComponent) and component.textureFilePath:
        return _validate_asset_path(f"{game_object_name}/Sprite.textureFilePath", component.textureFilePath)
    if isinstance(component, AnimatorComponent) and component.animationClipFilePath:
        return _validate_asset_path(
            f"{game_object_name}/Animator.animationClipFilePath",
            component.animationClipFilePath,
        )
    if isinstance(component, AudioComponent) and component.filePath:
        return _validate_asset_path(f"{game_object_name}/Audio.filePath", component.filePath)
    return []


def _validate_asset_path(location: str, value: str) -> list[ValidationIssue]:
    canonical_path = canonicalize_asset_path(value)
    if canonical_path is None:
        return [
            ValidationIssue(
                severity=Severity.ERROR,
                location=location,
                message=f"Rejected non-assets path '{value}'.",
            )
        ]
    if canonical_path != value:
        return [
            ValidationIssue(
                severity=Severity.WARNING,
                location=location,
                message=f"Path will normalize to '{canonical_path}'.",
            )
        ]
    return []