# Benchmark contract and smoke evidence (T071)

## Contract checks

The schema-4 writer/reader and sweep contracts are registered in CTest and
Python unittest. The latest full Release CTest run passed 55/55 after correcting
the schema-4 selection test to read `selection` and `sample_counts`. The focused
replay regression added for clustered batch transitions passed separately.
The reusable test logs are in [`final-validation/`](final-validation/).

The tested contracts and final evaluation use source fingerprint
`42d3e64f95876cb9fa0284bbf36b95a2e7e755692a1158ecc072e7f38ecec9dc`. The tests
cover schema-3 compatibility and explicit schema-4 dispatch; exact
metric applicability, numeric zero versus inapplicable blank, and unavailable
status; incomplete/truncated bundles; malformed traces and CLI conflicts;
deterministic case hashes and typed engine cases; relevant method expansion;
validation pairing including actual-render output signature; fresh-process
aggregation; destination refusal; and fallback ineligibility for the whole raw
run. The fingerprint test computes hashes in a disposable source tree and
proves that edits to `spatial_fixtures.cpp`, its header, and
`spatial_resource_hooks.cpp` each change the recorded fingerprint.

The dedicated resource-runner contract validates Release captures with C++
allocation hooks, Box2D allocator callbacks, owned-capacity snapshots, and
strict schema parsing. Replay snapshots include selected index, common-pair,
and scratch capacity immediately after initialization and after each complete
step, outside the measured replay interval. The contract checks initial/peak/
final values and domain applicability; the final replay resource bundle has
complete snapshots in all 248 eligible records. It also verifies that the
ordinary timing runner rejects resource mode. Resource outputs are
timing-ineligible. `nm -C` confirms that only
`platformator_spatial_resource_runner` defines allocation interceptors; the
timing runner has only the standard undefined `operator new` reference.

## Retained reproducible bundles

- The original compact independent replay smoke bundle is retained in
  [`spatial-replay-smoke/`](spatial-replay-smoke/).
- The full 41-workload replay preset, with a passed 248-configuration
  serial-profile validation matrix and 1,240 five-process timing captures, is
  retained in [`evaluation-timing/replay-cases-full/`](evaluation-timing/replay-cases-full/),
  [`evaluation-validation/replay-full-serial-final/`](evaluation-validation/replay-full-serial-final/),
  and [`evaluation-timing/replay-full-serial-final/`](evaluation-timing/replay-full-serial-final/).
- Its 248 linked resource records are retained in
  [`evaluation-resources/replay-full-serial-final/`](evaluation-resources/replay-full-serial-final/).
- The selected actual-engine validation, timing, and resource matrices are
  retained in [`evaluation-validation/engine-serial-final/`](evaluation-validation/engine-serial-final/),
  [`evaluation-timing/engine-serial-final/`](evaluation-timing/engine-serial-final/),
  and [`evaluation-resources/engine-serial-final/`](evaluation-resources/engine-serial-final/).

All sweeps retain raw samples, per-run summaries, manifests, commands/logs,
eligibility records, checksums, aggregate summaries and reports. Exact
limitations on actual-engine population coverage, hardware rendering, and
machine affinity are listed in [`us3-acceptance.md`](us3-acceptance.md).
The six final validation/timing/resource roots passed checksum checks. See
[`RESULTS.md`](RESULTS.md) for the paired measurements and
[`final-validation.md`](final-validation.md) for build/test status.
