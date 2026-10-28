# Benchmark settings metadata (T028)

The Release `platformator_spatial_selection_runner_test` passed against
`bin/cleanup-benchmark/platformator_benchmark_runner`. It invoked a real short benchmark capture
with `--broad-phase bvh --render-culling bvh --bvh-maintenance full-rebuild --bvh-padding 3
--bvh-query-execution serial`, then checked the generated manifest. Requested settings retain those
tokens and values; effective settings report BVH physics, no effective render culling for a run
without `--render`, the selected BVH policy, and `disabled_no_render`. The manifest reports zero
render samples. A nonfinite padding value fails instead of producing a mislabeled row.

`tests/spatial_selection_test.cpp` separately checks the shared runtime parser and benchmark option
parser with the same tokens, validation errors, defaults, positional `RuntimeOptions` aggregate
compatibility, and immutable runtime settings. Runner help states that `--render-culling` does not
enable `--render`. Schema-3 metric fields and complete-step/subscope timing boundaries remain
unchanged.
