from __future__ import annotations

import argparse
from pathlib import Path

from .app import launch_editor


def main() -> int:
    parser = argparse.ArgumentParser(description="Platformator scene editor and launcher")
    parser.add_argument("scene", nargs="?", help="Optional scene file to open on startup")
    arguments = parser.parse_args()

    scene_path = Path(arguments.scene).resolve() if arguments.scene else None
    return launch_editor(scene_path)


if __name__ == "__main__":
    raise SystemExit(main())
