# Platformator UI

`platformator-ui` is a Python desktop editor for Platformator scenes.

The first implementation in this folder focuses on three things:

- typed scene models and JSON round-tripping for Platformator scene files
- canonical `assets/...` path handling that matches the engine's current path policy
- a desktop shell that can open/save scenes and configure/build/run the engine

## Project Layout

```text
ui/
  pyproject.toml
  src/platformator_ui/
    app.py
    main.py
    main_window.py
    scene/
    engine/
    services/
    widgets/
  tests/
```

## Install

From the `ui/` directory:

```bash
python -m pip install -e .[dev]
```

## Run

From the repository root or from `ui/`:

```bash
python -m platformator_ui
```

or

```bash
platformator-ui
```

## Current Scope

The current shell supports:

- opening and saving Platformator scene JSON files
- generating a new starter scene with a camera
- object hierarchy browsing and basic object editing
- adding common engine components through a component palette
- browsing `assets/`
- configuring, building, and launching the engine with the selected scene

## Notes

- The engine currently launches scenes by passing a scene file path to `bin/main`.
- The editor normalizes known asset fields to `assets/...` on load/save.
- Legacy scene-relative paths such as `../ball.png` are migrated to canonical asset paths when the scene location makes that possible.
