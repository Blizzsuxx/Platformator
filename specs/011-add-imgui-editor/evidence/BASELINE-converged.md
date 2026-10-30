# Converged editor resource-capture identity freeze

This is a pre-capture identity freeze for the T100 resource workload. It is separate from the completed historical `BASELINE.md` and session. The earlier candidate freeze was invalidated by the SDL file-dialog fix and refreshed before any candidate session or capture was created. This file records candidate identity only; no resource or timing observations have been collected against it.

| Identity | Frozen value |
| --- | --- |
| Source SHA-256 | `08fb74be6bc0b718029543d79507b421add7e4842f979bf6453f040b5d85bf90` |
| Harness SHA-256 | `b98ff08f88b8ab716b58c7a2131c3a3d2154e12538bf21ca83427b8e49f10e60` |
| Workload SHA-256 | `57f53e8142e3d0d27e58ed54e56f9eb127fddd4971c1ef781e59e96ed48c5a4e` |
| Probe build | `bin/editor-release/platformator_editor_probe`, Release, GNU-16.2.1, C++20 |
| Probe binary SHA-256 | `867384e936e9cf72ab48dd5f4caa66776ce6212b0210dd3dfc8c58216e0f0a75` |
| Host | Linux x86_64, kernel `6.18.55-1-MANJARO`, AMD Ryzen 5 7535HS with Radeon Graphics, CPUs 0–11 |
| Resource workload | One declared warmup; 20 measured cycles; SDL dummy video/audio and software renderer; selected `main` Run/Stop lifecycle |
| Capture guard | Maximum 600 seconds and 50 MiB aggregate; preserve partial results; no automatic retry |
| Prepared session path | `specs/011-add-imgui-editor/evidence/editor-pilot-converged.session.json` (not created) |

The identity-bound resource command is listed in `RESULTS.md`. Starting it requires explicit authorization. Any source, harness, workload, probe or host identity change invalidates this freeze. The historical baseline and journal remain unchanged.
