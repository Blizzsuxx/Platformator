import json
from pathlib import Path

from platformator_ui.scene import SceneSerializer, ScriptComponentModel, SpriteComponent
from platformator_ui.scene.models import UnknownComponentModel


def test_scene_roundtrip_preserves_unknown_data_and_normalizes_known_asset_fields(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    scene_path = repo_root / "assets" / "scenes" / "level.scene"
    scene_path.parent.mkdir(parents=True)

    raw_scene = [
        {
            "id": 100,
            "rotation": 0.0,
            "active": True,
            "position": {"x": 12.0, "y": 34.0},
            "scale": {"x": 1.0, "y": 1.0},
            "name": "Player",
            "tag": "Hero",
            "children": [],
            "components": [
                {
                    "id": 200,
                    "textureFilePath": "../ball.png",
                    "flip": 1,
                    "width": 32.0,
                    "height": 32.0,
                    "sourceRectEnabled": False,
                    "type": 6,
                },
                {
                    "id": 201,
                    "type": 5,
                    "behaviors": [
                        {
                            "type": "AnnotatedSceneBehavior",
                            "icon": r"ASSETS\UI\PLAYER.PNG",
                            "idleClip": "assets/animations/player.animset",
                            "sound": "audio/jump.wav",
                            "displayName": "Player One",
                        }
                    ],
                },
                {
                    "id": 202,
                    "type": 999,
                    "customField": "preserve-me",
                },
            ],
        }
    ]
    scene_path.write_text(json.dumps(raw_scene), encoding="utf-8")

    scene_document = SceneSerializer.load(scene_path, repo_root=repo_root)
    player = scene_document.find_object_by_name("Player")
    assert player is not None

    sprite = player.find_component(SpriteComponent)
    assert sprite is not None
    assert sprite.textureFilePath == "assets/ball.png"

    script_component = player.find_component(ScriptComponentModel)
    assert script_component is not None
    behavior = script_component.behaviors[0]
    assert behavior.icon == "assets/ui/player.png"
    assert behavior.idleClip == "assets/animations/player.animset"
    assert behavior.sound == "assets/audio/jump.wav"

    unknown_component = next(component for component in player.components if isinstance(component, UnknownComponentModel))
    assert unknown_component.customField == "preserve-me"

    SceneSerializer.dump(scene_document, scene_path, repo_root=repo_root)
    saved_document = json.loads(scene_path.read_text(encoding="utf-8"))

    saved_components = saved_document[0]["components"]
    assert saved_components[0]["textureFilePath"] == "assets/ball.png"
    assert saved_components[1]["behaviors"][0]["icon"] == "assets/ui/player.png"
    assert saved_components[1]["behaviors"][0]["sound"] == "assets/audio/jump.wav"
    assert saved_components[2]["customField"] == "preserve-me"
