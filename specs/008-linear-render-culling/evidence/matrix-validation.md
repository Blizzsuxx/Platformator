# T044 Rendering and Resource Matrix

## Captures and renderer

The current proof roots are `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/validation/` and `resources/`. The 17 bounded cases each have pre-feature, candidate-disabled, and candidate-linear proofs: 51 rendering and 51 resource proofs. They use seed 0, 120 warmup steps, 600 measured steps, and `dt=0.008333333333333333`.

Rendered probes queried SDL and recorded renderer `software`, 320×240 output, scale `[1,1]`, default viewport, no active target/logical presentation, and VSync 0. The headless crossing cases report renderer `none`. Six-worker runs used the available six physical cores with process affinity 0–11.

| Workload | Sprite counts | Workers | Linear submitted fraction |
|---|---:|---:|---:|
| visible | 1, 1,000 | 1 | 1.000 |
| sparse | 100, 1,000, 10,000 | 1 | 0.100 |
| offscreen, clustered, camera-pan, churn, rotated | 1,000 each | 1 | 0.000, 0.100, 0.024, 0.500, 0.239 |
| crossing | 1,000 and 10,000 | 1 | 0.312 and 0.310 |
| visible 1,000; sparse 10,000; crossing 10,000 | as named | 6 | 1.000, 0.100, 0.310 |
| headless crossing | 1,000 | 1 and 6 | no rendering |

Corresponding rendered frames have identical normalized image hashes. Candidate-disabled traces match submit-all; candidate-linear traces preserve order and include every independently visible sprite. No duplicates, unexpected fallback, unresolved identities, or trace overflow were recorded. All-offscreen submits zero classifiable sprites. Headless proofs contain no image hashes or renderer and report zero render view, eligibility, visibility, and submission work.

The crossing proofs observed 6,393 entries / 6,121 exits at 1,000 sprites and 63,589 / 60,870 at 10,000 sprites over 600 steps; the six-worker 10,000 case has the same fixture counts. Camera-pan observed 891 entries / 875 exits. The fixture also validates the 1% per-step churn generations.

## Identity and audit

Baseline source fingerprint: `536cd40de0dea634250c56ffca20e68231ded659cef7257c9b8b9d60ee42529f`. Candidate source fingerprint: `4a5ce52e10fc81f903c3687879ee9d883836ec13668e18185f967fc6fa20dfba`. Shared fixture/harness hashes are `caff033f8a7577eb7d3f9eafe5a897bb09a70db78b385a7d010d4c174d84e65d` / `83f7119f120a7a3fd401028553ee29595b23e93a87f101b58ecb3611bc1abbd7`.

The proof files include frame JSONL, normalized image hashes, representative PPMs for the 45 rendered proof modes, and checksums. The separate resource proofs match the rendering source/harness and submission traces. `provenance/proof-verification.json` (SHA-256 `f996806d4bc3425cb46dec11f76bd16f9726b40707e9fbea8e865064c726cc75`) records proof identities; `provenance/timing-audit.json` independently rechecks them against every timing run. `scripts/verify_bundle.sh` repeats the full audit without relying on the original absolute path.
