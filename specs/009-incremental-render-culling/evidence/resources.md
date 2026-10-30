# Focused Pilot Resource Evidence

The 2026-10-29 pilot's retained `owned_bytes` and `peak_owned_bytes` observations are undercounts. `RenderCullingState::publishCapacityObservation` did not include the capacities of `visibleRankMerge` and `visibleRankChanges`, which are reserved alongside the other visible-selection buffers. At R=16,384, their combined capacity is approximately 384 KiB on the recorded build.

The pilot report, resource proofs, and their checksums remain unchanged. The published byte totals are lower bounds and do not establish complete culling-owned memory or satisfy SC-005. Allocation-call counts and RSS observations remain the observations recorded in the original bundle, with their existing observer limitations.

The current source now includes both scratch-vector capacities in the overflow-safe byte total. The focused regression reserves storage, reuses visibility-change batches, grows capacity, and checks release in cached-linear and indexed modes. It passed as the sole selected test:

```sh
cmake --build build/009-incremental-validation --target platformator_render_culling_index_test --parallel 4
ctest --test-dir build/009-incremental-validation -R '^platformator_render_culling_index_test$' --output-on-failure
```

This is an observer regression, not a corrected pilot resource capture. The saved pilot binaries and captured values predate the source correction, and the old values must not be relabeled as evidence for the corrected source identity. No resource-only capture was made.
