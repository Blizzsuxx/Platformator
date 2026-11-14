# Contract: Single-Level Template (`.pttemplate`) & Scene Instance Linkage

**Feature**: `016-final-editor-cleanup`
**Covers**: `FR-015`, `FR-016`, `FR-017`, `FR-018`, `FR-019`

## 1. Template Asset File (`.pttemplate`, Version 1)

Retains the existing root envelope (`format: "platformator.object-template"`, `version: 1`):

```json
{
  "format": "platformator.object-template",
  "version": 1,
  "name": "Goomba",
  "reference_bindings": [],
  "root": {
    "id": 101,
    "name": "Goomba",
    "tag": "enemy",
    "layer": 2,
    "active": true,
    "position": { "x": 0.0, "y": 0.0 },
    "rotation": 0.0,
    "scale": { "x": 1.0, "y": 1.0 },
    "components": [ ... ],
    "children": [ ... ]
  }
}
```

### Single-Level Flattening Guarantee
- No GameObject inside `TemplateAssetV1.root` (root or any descendant) may contain a `"template"` or `"templateNodeId"` linkage block.
- Saving a GameObject subtree as a template strips any inner `"template"` / `"templateNodeId"` fields recursively before writing `.pttemplate`, converting nested template instances into plain owned children/components of the new template.

## 2. Linked Scene Instance Metadata (`.scene`)

When a `.pttemplate` is instantiated into a scene (drag-drop onto Hierarchy/Scene or Hierarchy context menu -> Instantiate Template):

- The instantiated **root** GameObject stores a `"template"` object:
  ```json
  {
    "id": 5001,
    "name": "Goomba",
    "tag": "enemy",
    "layer": 2,
    "template": {
      "source": "assets/templates/Goomba.pttemplate",
      "nodeId": 101,
      "overrides": [
        "position",
        "components[id=1002].speed"
      ]
    },
    "components": [
      {
        "id": 5002,
        "templateNodeId": 1002,
        "type": 5,
        "speed": 3.5
      }
    ],
    "children": [
      {
        "id": 5003,
        "templateNodeId": 102,
        ...
      }
    ]
  }
  ```
- Each instantiated component and child descendant records `"templateNodeId"` matching its template-local `"id"` inside `.pttemplate` so subsequent sync passes match existing scene components/children cleanly without resetting unique scene `"id"` references.
- **Runtime Safety**: `SceneBuildContext` ignores unknown metadata fields (`"template"`, `"templateNodeId"`) during runtime GameObject construction, keeping runtime loading zero-overhead and backward compatible.

## 3. Instance Synchronization & Edit Operations

| Operation | Behavior |
|-----------|----------|
| Edit field on linked instance | Updates scene object + appends canonical field path to root's `template.overrides`. |
| **Revert Property** | Removes canonical path from `template.overrides` and restores current value from `.pttemplate`. |
| **Revert All** | Re-synchronizes full subtree (components, children, properties) to match `.pttemplate`, preserving root `"id"` (and root scene `position`? Note: `Revert All` reverts all overrides except keeping scene object identity/parenting). |
| **Apply to Template** | Writes non-structural/structural changes from this instance back to `.pttemplate` and clears its `overrides`, then triggers propagation to all other open instances. |
| **Unpack** | Removes `"template"` from root and `"templateNodeId"` from descendants; undoable via `EditCommandService`. |
| **Save `.pttemplate` in Template Editing Mode** | Validates & writes `.pttemplate`, then iterates all open scenes in workspace to add missing template nodes/components, remove deleted template nodes/components (preserving overridden ones if applicable or removing stale links), and update every non-overridden property. |
| **Missing `.pttemplate` file** | Validation logs a warning/error identifying the broken template path; Inspector shows `Missing Template` state with option to `Unpack` cleanly without losing scene data. |
