from __future__ import annotations

from .cmake_runner import ProcessSpec


def format_step_banner(process_spec: ProcessSpec) -> str:
    return f"\n$ {process_spec.display_command()}\n"
