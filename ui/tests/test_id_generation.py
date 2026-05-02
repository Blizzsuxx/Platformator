from platformator_ui.scene import GameObjectModel, SceneDocumentModel, SceneIdAllocator, SpriteComponent


def test_id_allocator_starts_after_highest_existing_object_or_component_id() -> None:
    scene = SceneDocumentModel(
        objects=[
            GameObjectModel(
                id=10,
                name="Ball",
                components=[SpriteComponent(id=25)],
            )
        ]
    )

    allocator = SceneIdAllocator.from_scene(scene)
    assert allocator.allocate() == 26
    assert allocator.allocate() == 27
