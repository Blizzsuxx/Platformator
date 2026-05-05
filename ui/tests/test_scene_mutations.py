from platformator_ui.scene import SceneIdAllocator, Vector2Model, add_game_object, create_empty_scene


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