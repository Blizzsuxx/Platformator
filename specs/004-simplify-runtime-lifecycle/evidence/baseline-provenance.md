# Baseline provenance

Frozen current-engine source: `/home/user/Projects/Platformator/bin/runtime-cleanup-validation/baseline-source`. Exact input commit, dirty patches, topology and tools are in `/home/user/Projects/Platformator/bin/runtime-cleanup-validation`.

Source/input hashes: `baseline-source-sha256.json`. Frozen engine archives: `baseline-archives/{cleanup-debug,cleanup-work,cleanup-benchmark}/libplatformator.a`. Baseline probe/runner binaries are copied after the initial build. Dependencies remain the unchanged vendored versions; no historic baseline is reused.

All six probe modes passed fixture checks. Connected chains passed 100/1000 bodies at six workers over 120 warmup + 600 invariant-checked steps; lifecycle replacement includes an explicit camera on an existing object to preserve the resolved population. Other modes also passed smoke checks. The driver lists 84 matrix groups and refuses invalid workers/existing destinations. Preliminary failed fixture attempts are retained separately under smoke/fixture-corrected; they are not accepted evidence.

Active fixture version: baseline-source-v2 and baseline-bin-v2. The first matrix exposed a mixed-population setup collision after sustained movement; disjoint fixture spacing is now 80, and wake/connected populations are asserted explicitly. Production source remains identical. Prior attempts are retained, not overwritten. Fixture and binary hashes: fixture-v2-sha256.json.
