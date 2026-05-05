from pathlib import Path

from platformator_ui.services.behavior_catalog import discover_behavior_templates


def test_discover_behavior_templates_excludes_test_sources(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    (repo_root / "src").mkdir(parents=True)
    (repo_root / "tests").mkdir()

    (repo_root / "src" / "player_behavior.h").write_text(
        "SERIALIZABLE_SCRIPT(PlayerBehavior, speed)\n",
        encoding="utf-8",
    )
    (repo_root / "tests" / "regression_behavior.h").write_text(
        "SERIALIZABLE_SCRIPT(RegressionOnlyBehavior, ticks)\n",
        encoding="utf-8",
    )

    templates = discover_behavior_templates(repo_root)

    assert "PlayerBehavior" in templates
    assert "RegressionOnlyBehavior" not in templates