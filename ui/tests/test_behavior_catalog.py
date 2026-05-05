from pathlib import Path

from platformator_ui.services.behavior_catalog import clear_behavior_discovery_caches, discover_behavior_templates


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


def test_discover_behavior_templates_infers_declared_defaults_without_scene_samples(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    (repo_root / "src").mkdir(parents=True)

    (repo_root / "src" / "player_behavior.h").write_text(
        """
class PlayerBehavior
{
    float speed;
    bool grounded;
    std::string displayName;

    SERIALIZABLE_SCRIPT(PlayerBehavior, speed, grounded, displayName);
};
""",
        encoding="utf-8",
    )

    clear_behavior_discovery_caches()
    templates = discover_behavior_templates(repo_root)

    assert templates["PlayerBehavior"] == {
        "speed": 0.0,
        "grounded": False,
        "displayName": "",
    }