> Historical pre-convergence record. Current implementation, qualification and completion evidence are in [convergence/IMPLEMENTATION.md](convergence/IMPLEMENTATION.md), [final-validation.md](final-validation.md) and [RESULTS.md](RESULTS.md). Earlier raw data/source identities remain unchanged.

# User Story 2 acceptance

The registered render geometry, image/readback, lifecycle, spatial-selection,
and runtime work-probe checks cover the eight physics/render selections. The
Release software-renderer run passed `platformator_render_culling_test`,
`platformator_render_culling_image_test`, `platformator_render_culling_lifecycle_test`,
`platformator_spatial_selection_runner_test`, and all other non-package CTest
tests in `bin/cleanup-benchmark` (53/53 passed; 7 probe-only tests are skipped
in this non-probe build). All 7 corresponding probe tests passed in the
`cleanup-work` configuration, including both physics worker policies and all
four render selections. The full package smoke also passed installed and
relocated consumers, source-embedded consumer, and the installed Mario example.

The image captures and pixel comparisons are retained in
[`render-images/`](render-images/); lifecycle checkpoints and reason/count
records are in [`render-lifecycle.md`](render-lifecycle.md) and
[`render-lifecycle.csv`](render-lifecycle.csv). The image fixture includes
transparent overlap, source rectangles, flips, resized animation frames,
swap removal/reactivation, sprite/collider independence, and the 10-of-1,000
sparse case. The lifecycle fixture includes hierarchy mutations, camera/view
changes, activation/replacement/destruction, zero-step rendering, headless and
no-camera resumption, and unsupported SDL view fallback.

Hardware-renderer parity has not been captured. Software output does not
qualify hardware timing or pixel validation.
