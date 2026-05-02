from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from platformator_ui.app import launch_editor
else:
    from .app import launch_editor


def main() -> int:
    parser = argparse.ArgumentParser(description="Platformator scene editor and launcher")
    parser.add_argument("scene", nargs="?", help="Optional scene file to open on startup")
    arguments = parser.parse_args()

    scene_path = Path(arguments.scene).resolve() if arguments.scene else None
    return launch_editor(scene_path)


if __name__ == "__main__":
    raise SystemExit(main())
