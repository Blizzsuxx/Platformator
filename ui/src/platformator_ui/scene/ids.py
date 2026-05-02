from __future__ import annotations

from dataclasses import dataclass

from .models import SceneDocumentModel


@dataclass
class SceneIdAllocator:
    next_value: int = 1

    @classmethod
    def from_scene(cls, scene: SceneDocumentModel) -> "SceneIdAllocator":
        highest_id = 0
        for game_object in scene.iter_objects():
            highest_id = max(highest_id, game_object.id)
            for component in game_object.components:
                highest_id = max(highest_id, component.id)
        return cls(next_value=highest_id + 1)

    def allocate(self) -> int:
        allocated = self.next_value
        self.next_value += 1
        return allocated
