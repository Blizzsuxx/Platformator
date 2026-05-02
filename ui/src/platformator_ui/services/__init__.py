from __future__ import annotations

from importlib import import_module
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .asset_paths import AssetKind
    from .project_paths import ProjectPaths

__all__ = [
    "AssetKind",
    "ProjectPaths",
    "canonicalize_asset_path",
    "normalize_scene_asset_paths",
    "normalize_scene_editor_asset_path",
]

_EXPORTS = {
    "AssetKind": (".asset_paths", "AssetKind"),
    "ProjectPaths": (".project_paths", "ProjectPaths"),
    "canonicalize_asset_path": (".asset_paths", "canonicalize_asset_path"),
    "normalize_scene_asset_paths": (".asset_paths", "normalize_scene_asset_paths"),
    "normalize_scene_editor_asset_path": (".asset_paths", "normalize_scene_editor_asset_path"),
}


def __getattr__(name: str) -> object:
    if name not in _EXPORTS:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

    module_name, attribute_name = _EXPORTS[name]
    module = import_module(module_name, __name__)
    value = getattr(module, attribute_name)
    globals()[name] = value
    return value
