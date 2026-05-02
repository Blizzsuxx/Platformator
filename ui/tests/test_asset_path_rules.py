from pathlib import Path

from platformator_ui.services.asset_paths import canonicalize_asset_path, normalize_scene_editor_asset_path


def test_canonicalize_asset_path_matches_engine_contract() -> None:
    assert canonicalize_asset_path(r"ASSETS\TEXTURES\DOES_NOT_EXIST.PNG") == "assets/textures/does_not_exist.png"
    assert canonicalize_asset_path("player/idle_0.png") == "assets/player/idle_0.png"
    assert canonicalize_asset_path("../ball.png") is None
    assert canonicalize_asset_path("/tmp/not_in_assets/default_cube.obj") is None


def test_legacy_scene_relative_path_can_be_migrated_when_scene_location_is_known(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    scene_path = repo_root / "assets" / "scenes" / "default.scene"
    scene_path.parent.mkdir(parents=True)
    scene_path.write_text("[]", encoding="utf-8")

    assert normalize_scene_editor_asset_path("../ball.png", scene_path=scene_path, repo_root=repo_root) == "assets/ball.png"
