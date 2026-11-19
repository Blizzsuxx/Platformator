# Render Order & Culling Pilot Report

- Repetitions: 5, Warmup: 10, Measured: 60
- Baseline binary: `/home/user/Projects/Platformator/build/render-order-baseline/platformator_linear_render_probe`
- Candidate timing: `/home/user/Projects/Platformator/build/render-order-timing/platformator_linear_render_probe`
- Candidate validation: `/home/user/Projects/Platformator/build/render-order-validation/platformator_linear_render_probe`

- Renderer mapping: backend=`software`, vsync=`1`, output=320x240

## Case: authored-static

- Validation: single-arm oracle check only (`candidate-cached-linear`: 60/60 frames checked against fixture oracle; inter-arm image parity not applicable with a single arm).
Note: `baseline-indexed` vs `candidate-cached-linear` is a combined implementation + policy endpoint comparison (pre-removal `indexed` vs current `cached-linear`).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| baseline-indexed | combined | unavailable | unavailable (frozen baseline) | 16.6648/16.6667/16.6745 | 16.6492 | 0.0051 |
| candidate-cached-linear | combined | 0.0006/0.0009 | 0.0004 | 16.6525/16.6666/16.6702 | 16.6364 | 0.0051 |

## Case: sparse-camera-pan

- Validation: single-arm oracle check only (`candidate-cached-linear`: 60/60 frames checked against fixture oracle; inter-arm image parity not applicable with a single arm).
Note: `baseline-indexed` vs `candidate-cached-linear` is a combined implementation + policy endpoint comparison (pre-removal `indexed` vs current `cached-linear`).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| baseline-indexed | combined | unavailable | unavailable (frozen baseline) | 16.6654/16.6668/16.6764 | 16.6473 | 0.0074 |
| candidate-cached-linear | combined | 0.0629/0.1085 | 0.0021 | 16.6534/16.6666/16.6719 | 16.6345 | 0.0085 |

## Case: dense-z-edit

- Validation: inter-arm image parity and oracle check across 4 arms (candidate-rebuild-w1, candidate-forced-sort-w1, candidate-repair-w1, candidate-rebuild-w4; 60/60 frames matched).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-rebuild-w1 | combined | 0.0006/0.0010 | 0.3527 | 16.6570/16.6666/16.6739 | 16.6399 | 0.0064 |
| candidate-rebuild-w1 | clean_frames | 0.0006/0.0010 | 0.0004 | 16.6174/16.6666/16.6737 | 16.6012 | 0.0059 |
| candidate-rebuild-w1 | edit_frames | 0.0006/0.0009 | 0.7050 | 16.6966/16.6665/16.6773 | 16.6786 | 0.0070 |
| candidate-forced-sort-w1 | combined | 0.0006/0.0011 | 0.6848 | 16.6527/16.6666/16.6757 | 16.6357 | 0.0061 |
| candidate-forced-sort-w1 | clean_frames | 0.0006/0.0010 | 0.6962 | 16.6604/16.6665/16.6813 | 16.6433 | 0.0058 |
| candidate-forced-sort-w1 | edit_frames | 0.0006/0.0012 | 0.6733 | 16.6451/16.6666/16.6724 | 16.6280 | 0.0064 |
| candidate-repair-w1 | combined | 0.0006/0.0010 | 0.0077 | 16.6543/16.6666/16.6753 | 16.6378 | 0.0062 |
| candidate-repair-w1 | clean_frames | 0.0006/0.0011 | 0.0004 | 16.6721/16.6666/16.7048 | 16.6551 | 0.0060 |
| candidate-repair-w1 | edit_frames | 0.0006/0.0010 | 0.0149 | 16.6365/16.6665/16.6712 | 16.6204 | 0.0065 |
| candidate-rebuild-w4 | combined | 0.0006/0.0010 | 0.3493 | 16.6527/16.6666/16.6749 | 16.6359 | 0.0063 |
| candidate-rebuild-w4 | clean_frames | 0.0006/0.0009 | 0.0004 | 16.6194/16.6666/16.6749 | 16.6025 | 0.0058 |
| candidate-rebuild-w4 | edit_frames | 0.0006/0.0010 | 0.6981 | 16.6861/16.6666/16.6753 | 16.6693 | 0.0067 |
