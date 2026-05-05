from platformator_ui.scene import SceneIdAllocator, ScriptBehaviorModel, ScriptComponentModel, Vector2Model, add_game_object, create_empty_scene, synchronize_behavior_fields


def test_add_game_object_under_parent_uses_local_default_offset() -> None:
    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)

    parent = add_game_object(
        scene_document,
        allocator,
        name="Parent",
        position=Vector2Model(x=100.0, y=200.0),
    )

    child = add_game_object(scene_document, allocator, parent_id=parent.id, name="Child")

    assert parent.children == [child]
    assert child.position.x == 32.0
    assert child.position.y == 32.0


def test_add_game_object_under_parent_preserves_explicit_local_position() -> None:
    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)

    parent = add_game_object(
        scene_document,
        allocator,
        name="Parent",
        position=Vector2Model(x=100.0, y=200.0),
    )

    child = add_game_object(
        scene_document,
        allocator,
        parent_id=parent.id,
        name="Child",
        position=Vector2Model(x=-16.0, y=24.0),
    )

    assert parent.children == [child]
    assert child.position.x == -16.0
    assert child.position.y == 24.0


def test_synchronize_behavior_fields_adds_missing_and_removes_stale_fields() -> None:
    scene_document = create_empty_scene(include_default_camera=False)
    allocator = SceneIdAllocator.from_scene(scene_document)
    game_object = add_game_object(scene_document, allocator, name="Player")

    behavior = ScriptBehaviorModel.model_validate({
        "type": "PlayerBehavior",
        "speed": 12.5,
        "legacy": 3,
    })
    script_component = ScriptComponentModel(id=allocator.allocate(), behaviors=[behavior])
    game_object.components.append(script_component)

    changed = synchronize_behavior_fields(
        scene_document,
        {"PlayerBehavior": {"speed": 0.0, "grounded": False}},
    )

    assert changed is True
    assert behavior.speed == 12.5
    assert behavior.grounded is False
    assert "legacy" not in (behavior.model_extra or {})