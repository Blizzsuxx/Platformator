# Retained captures and reproducibility

Origin runtime commit: `daa911d40ce00a882d8dffbccfa0d55cfde7d5be`. Initial runtime/source patches were empty; the initial untracked feature documents and environment are recorded in bin/runtime-cleanup-validation/input-status.txt and environment.json. No user source edit was overwritten and no Git branch/commit was created.

Current production source hashes: candidate-source-sha256.json. Original production hashes: baseline-source-sha256.json (the original preliminary probe hash is historical; active probe/driver/binary hashes are explicitly in common-fixture-sha256.json). Apply common-validation-fixtures.patch to the origin commit to recreate the shared validation setup; it changes tests/probe/CMake plumbing, not the baseline runtime implementation. Root baseline-final-source proves original src/include/cmake content with current test-only overlays.

Raw capture root: `/home/user/Projects/Platformator/bin/runtime-cleanup-validation`.

- `baseline-source-v2`, `baseline-final-source`, `baseline-archives`, `baseline-bin-v2`: retained baseline inputs and binaries.
- `final-validation-v2`: final paired 168-process correctness capture.
- `final-time-v2`: final 840-process timing capture; schedule, raw CSV/samples/manifests/logs/checksums.
- `final-time`: historical first-candidate timings, before the lookup correction.
- `identical-binary-control`: 480-process A/A timing control.
- `persistent-followup`: 240-process 20-pair follow-up on the marginal contact case.
- `lookup-micro-results.json`: 45-process warmed lookup comparison; boundary is not complete_step.
- `baseline-sanitizer-objects`, `baseline-overlay`: fully instrumented old-core provenance and current-test overlays.
- `lookup-v2-{debug,asan,tsan}.xml` and `logs/`: final results and actual safety limitations.

Do not remove the ignored capture root before archiving it. These are retained local artifacts, not a claim they have been published or committed. The source commit, fixture patch, source/binary hashes and per-group summaries in this feature preserve audit provenance separately from bulky raw files.

A compact raw/source evidence archive is retained as `bin/runtime-cleanup-validation/final-evidence.tar.gz`; archive-sha256.json records its size and digest. Earlier diagnostic attempts and full sanitizer logs/objects remain in the raw root, not inside this compact archive.
