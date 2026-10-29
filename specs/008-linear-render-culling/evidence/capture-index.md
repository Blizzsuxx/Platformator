# Persistent T044 Capture Index

## Current verified run

The complete T044 evidence bundle is stored in the Git-ignored directory `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`. The ignore rule was checked before creating the run directory. Its source snapshots, builds, binaries, 51 rendering proofs, 51 resource observations, 315 timing/control captures, raw samples, commands, logs, and provenance are all inside that directory; no required input or result depends on `/tmp`. The bundle entry point is [provenance/ARCHIVE_INDEX.md](../../../benchmark/artifacts/linear-render-culling/20261029-t044-run-01/provenance/ARCHIVE_INDEX.md); `provenance/artifact-files.sha256` checksums every other regular file.

| Bundle path | Contents |
|---|---|
| `baseline/source/` | Pinned commit archive and isolated pre-feature source with only common fixture/build/probe and observation support applied. |
| `candidate/source/` | Candidate source snapshot and its retained archive. |
| `baseline/build-{validation,timing}/`, `candidate/build-{validation,timing}/` | Separate Release work-probe validation and uninstrumented timing builds, focused probes, and stock runners. |
| `validation/`, `resources/` | 51 image/order proof sets and 51 matching resource proofs. Headless cases have no image file. |
| `timing/final/` | 255 focused timing runs, 60 stock-engine controls, schema-v3 raw samples, manifests, logs, `report.json`, and `raw-index.csv`. |
| `provenance/` | Source-input manifests/audit, binary identities, baseline patch, correctness/timing verification, performance analysis, relocation views, and checksums. |
| `scripts/` | Baseline reconstruction, isolated build, correctness, proof, timing, audit, relocation, reproduction, and checksum commands. |

Capture parameters: 17 cases, three modes, five fresh processes per comparison, seed 0, 120 warmup steps, 600 measured steps, `dt=0.008333333333333333`; worker counts 1 and 6, affinity CPUs 0–11. The queried renderer was SDL `software`, 320×240, scale 1×1, default viewport, no active target/logical presentation, VSync off. Headless cases report no renderer.

The fresh capture checksums are:

- `timing/final/report.json`: SHA-256 `ed7e25acbcaf21f88ef569ec70d4a8d5685d88988a470cd6ca47e0c76bba0a10`.
- `timing/final/raw-index.csv`: SHA-256 `7ffc42a7c93ed1057536b4d55e1866ad7e4c33e195010b417b0c6b3f3bb7a3e3`.
- `provenance/source-manifest-audit.json`: SHA-256 `88b2549deab186eccffd0f397d33bd3c56fcdbd0f8f78665ddc3ec91e56d3fad`.
- `provenance/binary-identities.json`: SHA-256 `2bf4def7dbab76755a8ca87c120c80e34cb22f2b2e58cc1d7a64f69da4139fec`.
- `provenance/proof-verification.json`: SHA-256 `f996806d4bc3425cb46dec11f76bd16f9726b40707e9fbea8e865064c726cc75`.
- `provenance/timing-audit.json`: SHA-256 `e289349f64db1efc253b8147bb6dba0278abb08122c5270f9d4ff70af83ea31b`.
- `provenance/performance-analysis.json`: SHA-256 `4c0456f2e019c2b23325b5400f79f9ff645ca313d13167119bbb4ee7bc3ecba6`.

`provenance/artifact-files.sha256` is the checksum inventory for the full bundle (excluding the inventory file itself). `provenance/relocated-view/index.json` maps the original report/raw-index checksums to bundle-relative report and raw-index copies. The index and inventory currently cover 43,509 regular files. To verify from the bundle root, run `scripts/verify_bundle.sh`. To relocate the directory, move it as a unit and run `python3 scripts/relocate_index.py`; the original captured report and raw index remain byte-for-byte unchanged.

To regenerate in a fresh ignored directory, run `scripts/reproduce.sh benchmark/artifacts/linear-render-culling/<new-run-id>`. It checks the destination against Git's ignore rules before creating it, copies only the retained baseline/candidate source inputs and scripts, then builds, checks correctness, regenerates proofs, runs timings serially, and writes its own checksums. The output path must not already exist.

## Superseded capture summaries

The old final path `/tmp/platformator-linear-render-culling-final-20261029/`, the earlier full-matrix path `/tmp/platformator-linear-render-culling-20261029-fullmatrix/`, and the initial `/tmp/platformator-linear-render-culling-20261028/` tree were absent when T044 recovery began. The hashes below are claims recorded in the previous summaries, not checksums of files available in this run:

| Superseded summary | Historical report/index hashes |
|---|---|
| Previous final summary | report `f295620550dee814347ce8db24f9a1a955d802155885412d1634569dd8f51ace`; raw index `905f8025e9746da389d7883a4cfea0777150daede76e5f24826da0380c34fc09`. |
| Earlier full matrix | report `4903a0892c1a7f8ecfc38c7c3478f68018d64c3e696a776f3df92d0203176993`; raw index `65e6f24fd0ba9113f49a1a00edb67aa875a99000bad7cc73ba719dc0fdee1f14`. |
| Historical ten-run broad-phase diagnostic | report `6f843e44858e4623bb940c34a3ab6e8ce343b69158e8d98ab291e3fc4d802ad1`; runs `30d793da327f864a260be79b5cda6025fc2a01f63da17493dbf24fc3adf825b3`; commands `d3476a005336bdd1031dee02c03cb6ac265e2d8c647b48283d7075a0f89bfec8`. |

Exact pre-T044 Markdown summaries are preserved under `provenance/superseded-summaries/` with their own SHA-256 list. Those snapshots preserve historical narrative only; the old raw captures, binaries, proofs, and manifests were not recovered. The regenerated 20261029 run is the current evidence and is separately indexed above.
