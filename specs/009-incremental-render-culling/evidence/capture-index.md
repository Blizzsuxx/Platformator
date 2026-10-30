# Incremental Render Culling Capture Index

## Durable run root

- Run root: `benchmark/artifacts/linear-render-culling/009-20261029-run-01/`; the repository ignore rule covers it.
- The frozen feature-008 reference and its original source archive remain under `reference/`. T044 historical timing and validation artifacts were not edited.
- The old `validation/` tree is a retained partial campaign, not a complete current validation phase. Its historical output is described in [coverage.md](coverage.md). The first `candidate/build-validation` and `candidate/build-timing` binaries retain their original source identities; the pilot binaries use distinct `*_pilot_final` names.
- The current focused run is `captures/pilot-final/`, indexed by `pilot-final-index.json`. It contains six cases, 54 timing processes, 24 resource proofs, all schema-v3 samples, paired intervals, logs, and report. The index also checksums the retained pilot-input manifest and all 151 preserved input files, plus the replay command and its two probe launchers.
- Three earlier focused attempts under `captures/pilot/`, `captures/pilot-verified/`, and `captures/pilot-verified-2/` stopped before writing a success report. They remain intact to preserve the verifier-fix history; only `pilot-final/` is used in [RESULTS.md](RESULTS.md).
- The original `candidate/source/Platformator/` snapshot differs from the final pilot input manifest in ten paths. It remains untouched as historical provenance. The exact 151 manifest inputs (3,599,358 bytes) are preserved separately under `candidate/source/pilot-inputs/`; all file hashes match aggregate `5a500fe96e6cb99acc9af6e81c60f61710380c0e1d14362e84d602d9bb53dcf8`.
- `candidate/source/pilot-inputs/` is the immutable input set for the saved pilot identity, not the current post-pilot workspace. The later culling-owned memory-observer correction changes source identity; it has not been rebuilt or substituted into the historical captures. The retained pilot byte totals also omit two selection scratch buffers, as documented in [resources.md](resources.md).

## Current pilot source, harness, and binaries

- Candidate source SHA-256: `5a500fe96e6cb99acc9af6e81c60f61710380c0e1d14362e84d602d9bb53dcf8`.
- Fixture SHA-256: `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`.
- Harness SHA-256: `bd85667c67f3a7b0743de854bc248cb1d1f64e6c0beb0d94d360d9898d2976dd`.
- Capture-driver SHA-256: `beba5c78dd8e2e97d21c1b8596e4d4231aa8da0224c5b0253a9242a5032a855d`; source copy is under `provenance/pilot-capture_linear_render.py`.
- Validation and timing probes share source/fixture/harness identities. Both are Release, debug tools off, sanitizer none; validation enables work probes and timing disables them. Exact identities and source-input manifests are in the `*_final.json` / `*_final.manifest` proofs beside each pilot binary.
- Timing probe SHA-256: `bbb10bc78f669a1ec48ac51f4cf4e93a517de4b8f1991dc05907e064fe4ca519`.
- Validation probe SHA-256: `d0217eab248bd5b6bf91e3b5b5f9064a3ce5292556a5a1e37a0a30f2c403da5a`.
- Asset tree SHA-256: `f2a813955fb1a9ac270b18c88a722c0090cd9aecaedd65108641240605c6be02`.
- The saved pilot-input manifest is `candidate/source/pilot-inputs/pilot-source-inputs-final.manifest`; its paths are relative to `candidate/source/pilot-inputs/` and it includes the aggregate source identity. It preserves the capture driver, Python helper, exact assets, and all fingerprinted source inputs without duplicating build trees or historical captures.

## Host and capture settings

- Date: 2026-10-29; OS: Manjaro Linux rolling, x86_64; kernel `6.18.55-1-MANJARO`.
- CPU: AMD Ryzen 5 7535HS, 6 physical cores / 12 logical CPUs. The pilot was not pinned; manifests record affinity `0–11`.
- Software renderer, 320×240 output, VSync off, eligible 1:1 output mapping.
- One-worker cases use 1 worker; the selected crossing repeat uses 6 workers. Seed 0, `dt=0.008333333333333333`, 5 warmup and 30 measured steps, 3 independent processes per strategy/case.
- Timing rows did not overlap builds, CTest, or resource captures.

## Verification

Run `benchmark/artifacts/linear-render-culling/009-20261029-run-01/scripts/verify_pilot.sh`. It checks the report/index/raw file checksums, schema-v3 timing summaries against raw samples, paired values and process aggregates, resource-proof links, probe build identities, and locally supplied runtime dependencies. The focused pilot subset passed this verifier both in place and from a temporary relocated copy containing only its indexed reports, proofs, samples, binaries, manifests, verifier, and required shared libraries.

This relocation check does not validate the full historical bundle and does not close SC-007 for feature acceptance. The bundle verifier checks all indexed files, including the 151 preserved inputs. To verify the source inputs before a rerun, use `commands/pilot-capture.sh --dry-run <fresh-output-directory>`; it checks all per-file hashes and the aggregate against the timing build identity, then confirms each probe resolves SDL and oneTBB from its matching bundle build. The `commands/launch-pilot-validation-probe.sh` and `commands/launch-pilot-timing-probe.sh` wrappers set those per-build library paths, avoiding the binaries' original absolute RUNPATHs into the live checkout. System runtime libraries remain host prerequisites.

To run a new exploratory capture, invoke `commands/pilot-capture.sh <fresh-output-directory>`. It uses the preserved driver/assets and saved probes, and refuses to overwrite an existing output path. To rebuild, overlay `candidate/source/pilot-inputs/` on a clean checkout and follow the recorded build commands; this source overlay contains fingerprinted inputs only and is not a Git checkout. The existing report verifier checks captured evidence, while the capture script's dry-run checks reproduction inputs. Neither operation closes SC-006 or the full SC-007 acceptance gate.

The focused measurements and their limits are summarized in [RESULTS.md](RESULTS.md). SC-006 and the original exhaustive T040–T046 closure remain open.
