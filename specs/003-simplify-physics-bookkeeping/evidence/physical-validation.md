# Physical and worker comparisons

All36 baseline and36 candidate per-step independent-oracle captures pass. Each stage's18 one/six-worker canonical snapshot comparisons pass, as do all36 baseline/candidate group comparisons. Raw checksums were independently reverified after capture. Script checks matching compiler/flags, configuration, instrumentation, hardware/dependencies, boundaries and resolved populations. Baseline and candidate shared-library isolation is independently checked in isolated-runtime-libraries.txt.

Exact identity/support/sleep/features and existing position/velocity/separation tolerance1e-4, rotation/angular velocity/normals1e-5 are retained. Per-step oracle is labeled timing_eligible=false; no durations here are performance evidence. Ordered event histories are separately checked by contact/bookkeeping/support fixtures.

Raw data: bin/physics-bookkeeping-20261025/captures/{baseline,candidate}/validate. Baseline source5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031, candidate6d095977a927bd8170521f61894abd01eabd8dc2349d3b376926771e96de4c41. Both are schema 3; no historical event semantics are relabeled.

Final candidate after reset fix repeats all36 captures,18 worker comparisons and36cross-stage comparisons successfully at captures-v2. The immutable baseline36 validation files are copied with their checksums unchanged; final candidate source fingerprint b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db. All raw checksums reverified. Initial candidate results above remain historical context, not substituted for final verification.
