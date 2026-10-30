# Converged editor resource-capture identity freeze

**Invalidated pre-capture identity freeze** for the T100 resource workload. It is separate from the completed historical `BASELINE.md` and session. The candidate identity was refreshed after the SDL file-dialog and optional-asset validation fixes, before any candidate session or capture was created. Subsequent GUI toolbar and hierarchy changes altered the source, so these values are retained only as the pre-cleanup identity record and must not be used for a capture. No replacement Release build or identity freeze was created, and no resource or timing observations have been collected.

| Identity | Frozen value |
| --- | --- |
| Source SHA-256 | `fe5c010ebb92dd70aa6da677e9d7e9d3b1ca4c90cd1a7e488e1d6404d16aab72` |
| Harness SHA-256 | `b98ff08f88b8ab716b58c7a2131c3a3d2154e12538bf21ca83427b8e49f10e60` |
| Workload SHA-256 | `57f53e8142e3d0d27e58ed54e56f9eb127fddd4971c1ef781e59e96ed48c5a4e` |
| Probe build | `bin/editor-release/platformator_editor_probe`, Release, GNU-16.2.1, C++20 |
| Probe binary SHA-256 | `a2d3b3642f97a441f9afacc735ae5f507e5e105ba6cc97336b72aceeed6e0829` |
| Host | Linux x86_64, kernel `6.18.55-1-MANJARO`, AMD Ryzen 5 7535HS with Radeon Graphics, CPUs 0–11 |
| Resource workload | One declared warmup; 20 measured cycles; SDL dummy video/audio and software renderer; selected `main` Run/Stop lifecycle |
| Capture guard | Maximum 600 seconds and 50 MiB aggregate; preserve partial results; no automatic retry |
| Prepared session path | `specs/011-add-imgui-editor/evidence/editor-pilot-converged.session.json` (not created) |

The identity-bound resource command is listed in `RESULTS.md`. Starting it requires explicit authorization. Any source, harness, workload, probe or host identity change invalidates this freeze. The historical baseline and journal remain unchanged.
