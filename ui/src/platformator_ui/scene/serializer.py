from __future__ import annotations

from pathlib import Path

import orjson

from platformator_ui.services.asset_paths import normalize_scene_asset_paths

from .models import GameObjectModel, SceneDocumentModel


class SceneSerializationError(ValueError):
    pass


class SceneSerializer:
    @staticmethod
    def load(path: Path, *, repo_root: Path | None = None) -> SceneDocumentModel:
        return SceneSerializer.loads(path.read_bytes(), source_path=path, repo_root=repo_root)

    @staticmethod
    def loads(
        payload: bytes | str,
        *,
        source_path: Path | None = None,
        repo_root: Path | None = None,
    ) -> SceneDocumentModel:
        if isinstance(payload, str):
            payload = payload.encode("utf-8")

        raw_document = orjson.loads(payload)
        if not isinstance(raw_document, list):
            raise SceneSerializationError("Platformator scenes must be a top-level JSON array.")

        scene_document = SceneDocumentModel(
            objects=[GameObjectModel.model_validate(game_object) for game_object in raw_document],
            source_path=str(source_path) if source_path is not None else None,
        )
        normalize_scene_asset_paths(scene_document, scene_path=source_path, repo_root=repo_root)
        return scene_document

    @staticmethod
    def dump(
        scene_document: SceneDocumentModel,
        path: Path,
        *,
        repo_root: Path | None = None,
        canonicalize_assets: bool = True,
    ) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(
            SceneSerializer.dumps(
                scene_document,
                source_path=path,
                repo_root=repo_root,
                canonicalize_assets=canonicalize_assets,
            )
        )

    @staticmethod
    def dumps(
        scene_document: SceneDocumentModel,
        *,
        source_path: Path | None = None,
        repo_root: Path | None = None,
        canonicalize_assets: bool = True,
    ) -> bytes:
        scene_copy = scene_document.model_copy(deep=True)
        if canonicalize_assets:
            normalize_scene_asset_paths(scene_copy, scene_path=source_path, repo_root=repo_root)

        document = [game_object.model_dump(mode="json", exclude_none=False) for game_object in scene_copy.objects]
        return orjson.dumps(document, option=orjson.OPT_INDENT_2 | orjson.OPT_SORT_KEYS)

    @staticmethod
    def to_pretty_string(
        scene_document: SceneDocumentModel,
        *,
        source_path: Path | None = None,
        repo_root: Path | None = None,
        canonicalize_assets: bool = False,
    ) -> str:
        return SceneSerializer.dumps(
            scene_document,
            source_path=source_path,
            repo_root=repo_root,
            canonicalize_assets=canonicalize_assets,
        ).decode("utf-8")
