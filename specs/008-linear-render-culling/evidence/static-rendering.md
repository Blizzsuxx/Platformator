# Static Rendering Evidence

The scenario-level image/order checks below are the earlier correctness summary; their standalone PPM directory from 2026-10-28 was absent during T044 recovery. The current T044 candidate CTest passed, and durable representative images for all rendered bounded-matrix proofs are retained under `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/validation/`. Their per-proof SHA-256 values and frame hashes are checked by `scripts/verify_bundle.sh`. The older standalone PPM hashes below are historical summary values, not files claimed to be retrievable in this run.

## Environment and commands

- Candidate revision was built from the feature worktree using the `cleanup-debug` and `cleanup-work` presets.
- Correctness renderer: SDL `software`, explicitly selected by `cmake/PlatformatorTests.cmake` with dummy video/audio. The readback test queried `SDL_GetRendererName` and received `software` for both modes. `SDL_GetRenderVSync` reported `0` for each frame. Output was 320×240.
- Geometry check: `ctest --test-dir bin/cleanup-debug -R '^platformator_render_culling_geometry_test$' --output-on-failure` and the matching work-probe test both passed.
- Image/order check: `bin/cleanup-work/platformator_render_culling_test` passed. It captures normalized RGBA pixels through the scoped callback before presentation, compares enabled and submit-all frames byte for byte, and checks submission traces against the independent corner oracle.
- Public option parser: passed in ordinary debug and work-probe debug; the ordinary image/order test reports the unavailable work-probe portion as skipped (CTest skip code 77), rather than passing it silently.
- The acceptance test first failed against the unchanged submit-all path because the all-offscreen candidate still submitted sprites. After integration, it passed with zero all-offscreen submissions.

## Fixture identities, pixel hashes, and submissions

All identities are logical `{slot:generation}` pairs assigned by the fixture; pointers are only resolved inside a single runtime. Each scenario starts at generation 0. Image hashes below are FNV-1a over normalized RGBA bytes; the PPM hashes are SHA-256 of the saved RGB representative images.

| Case | Submit-all trace | Enabled trace | Result | RGBA FNV-1a |
|------|-----------------|---------------|--------|-------------|
| All visible | `[0:0, 1:0]` | `[0:0, 1:0]` | Exact image match; both retained | `03f9f35f1e576459` |
| All offscreen | `[0:0, 1:0, 2:0]` | `[]` | Exact blank image match; zero candidate submissions | `24cda718d8961325` |
| Inactive | `[]` | `[]` | Exact image match | `24cda718d8961325` |
| Empty | `[]` | `[]` | Exact image match | `24cda718d8961325` |
| Mixed/translucent/transform/source/flip/missing texture | `[0:0, 1:0, 2:0, 3:0, 4:0, 5:0]` | `[0:0, 2:0, 3:0, 4:0, 5:0]` | Exact image match; order retained; offscreen textured slot 1 rejected; offscreen null-texture slot 5 submitted conservatively | `e4e7cae3c7c7b064` |
| Unequal camera/output scales | `[0:0, 1:0, 2:0]` | `[0:0, 1:0]` | Exact image match; camera scales 0.5×2.0 | `7c1d12f7dd4d457d` |

The previous standalone PPM files and their `/tmp` directory were not recovered. Current representative PPMs are generated per rendered proof in the T044 `validation/` directory; proof JSON stores each SHA-256 and frame trace checksum. Headless proof modes correctly have no PPM.

## Work counts and numeric policy

- All-visible: one prepared view, one eligibility check, two visibility checks, two submissions.
- All-offscreen: one prepared view, one eligibility check, three visibility checks, zero submissions.
- Mixed: one prepared view, one eligibility check, five checks for classifiable textured sprites, five submissions including the offscreen null-texture sprite.
- The helper uses the already prepared destination rectangle. Zero rotation avoids trigonometry. Other angles use SDL's degree-derived float sine/cosine path, then expand outward with `nextafter` and at least a one-output-pixel guard. A small float-rounding allowance is included; coordinates whose float spacing could consume the guard remain unclassifiable and are submitted.
- The independent double corner oracle covered 5,000 fixed-seed valid randomized inputs, edge and boundary cases, unequal X/Y scales, quadrant and near-quadrant angles, elongated/oversized sprites, and fractional coordinates. Invalid, non-finite, zero/negative, ineligible, and extreme-magnitude inputs were required to return `Unclassifiable`; the work-probe path then submits them.
- No missing-pixel, duplicate, or order-inversion failures were observed. This is static software-renderer correctness evidence only, not dynamic lifecycle or performance evidence.
