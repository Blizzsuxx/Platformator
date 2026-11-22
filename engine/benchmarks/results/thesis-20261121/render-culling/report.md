# Render Order & Culling Pilot Report

- Repetitions: 3, Warmup: 5, Measured: 30
- Baseline binary: none (candidate-only capture)
- Candidate timing: `/home/user/Projects/Platformator/build/benchmark/platformator_linear_render_probe`
- Candidate validation: `/home/user/Projects/Platformator/build/render-order-validation/platformator_linear_render_probe`

- Renderer mapping: backend=`software`, vsync=`1`, output=320x240

## Case: authored-static

- Validation: single-arm oracle check only (`candidate-cached-linear`: 30/30 frames checked against fixture oracle; inter-arm image parity not applicable with a single arm).
Note: candidate-only `cached-linear` capture (no frozen `--baseline-probe` supplied).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-cached-linear | combined | 0.0006/0.0011 | 0.0004 | 16.6414/16.6665/16.6705 | 16.6241 | 0.0068 |

## Case: sparse-camera-pan

- Validation: single-arm oracle check only (`candidate-cached-linear`: 30/30 frames checked against fixture oracle; inter-arm image parity not applicable with a single arm).
Note: candidate-only `cached-linear` capture (no frozen `--baseline-probe` supplied).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-cached-linear | combined | 0.0583/0.0921 | 0.0020 | 16.6401/16.6666/16.6678 | 16.6217 | 0.0089 |

## Case: dense-z-edit

- Validation: inter-arm image parity and oracle check across 4 arms (candidate-rebuild-w1, candidate-forced-sort-w1, candidate-repair-w1, candidate-rebuild-w6; 30/30 frames matched).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-rebuild-w1 | combined | 0.0006/0.0009 | 0.3518 | 16.6417/16.6664/16.6686 | 16.6247 | 0.0078 |
| candidate-rebuild-w1 | clean_frames | 0.0006/0.0009 | 0.0004 | 16.6169/16.6662/16.6686 | 16.6003 | 0.0074 |
| candidate-rebuild-w1 | edit_frames | 0.0006/0.0011 | 0.7033 | 16.6665/16.6666/16.6683 | 16.6492 | 0.0081 |
| candidate-forced-sort-w1 | combined | 0.0006/0.0009 | 0.7255 | 16.6420/16.6665/16.6714 | 16.6245 | 0.0077 |
| candidate-forced-sort-w1 | clean_frames | 0.0006/0.0009 | 0.7339 | 16.6163/16.6663/16.6736 | 16.6004 | 0.0066 |
| candidate-forced-sort-w1 | edit_frames | 0.0006/0.0008 | 0.7171 | 16.6677/16.6666/16.6678 | 16.6487 | 0.0088 |
| candidate-repair-w1 | combined | 0.0006/0.0009 | 0.0071 | 16.6442/16.6664/16.6692 | 16.6274 | 0.0074 |
| candidate-repair-w1 | clean_frames | 0.0006/0.0009 | 0.0004 | 16.6220/16.6662/16.6692 | 16.6066 | 0.0067 |
| candidate-repair-w1 | edit_frames | 0.0006/0.0009 | 0.0138 | 16.6663/16.6666/16.6682 | 16.6481 | 0.0081 |
| candidate-rebuild-w6 | combined | 0.0006/0.0011 | 0.3408 | 16.6385/16.6664/16.6727 | 16.6206 | 0.0075 |
| candidate-rebuild-w6 | clean_frames | 0.0006/0.0012 | 0.0004 | 16.6083/16.6664/16.6703 | 16.5907 | 0.0073 |
| candidate-rebuild-w6 | edit_frames | 0.0006/0.0010 | 0.6813 | 16.6687/16.6665/16.6740 | 16.6505 | 0.0077 |
