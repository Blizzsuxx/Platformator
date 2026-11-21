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
| candidate-cached-linear | combined | 0.0006/0.0009 | 0.0004 | 16.6371/16.6664/16.6676 | 16.6227 | 0.0057 |

## Case: sparse-camera-pan

- Validation: single-arm oracle check only (`candidate-cached-linear`: 30/30 frames checked against fixture oracle; inter-arm image parity not applicable with a single arm).
Note: candidate-only `cached-linear` capture (no frozen `--baseline-probe` supplied).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-cached-linear | combined | 0.0530/0.0813 | 0.0018 | 16.6393/16.6666/16.6683 | 16.6263 | 0.0061 |

## Case: dense-z-edit

- Validation: inter-arm image parity and oracle check across 4 arms (candidate-rebuild-w1, candidate-forced-sort-w1, candidate-repair-w1, candidate-rebuild-w6; 30/30 frames matched).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-rebuild-w1 | combined | 0.0006/0.0009 | 0.3593 | 16.6350/16.6665/16.6685 | 16.6205 | 0.0059 |
| candidate-rebuild-w1 | clean_frames | 0.0006/0.0009 | 0.0004 | 16.6046/16.6666/16.6731 | 16.5911 | 0.0054 |
| candidate-rebuild-w1 | edit_frames | 0.0006/0.0009 | 0.7183 | 16.6654/16.6664/16.6679 | 16.6500 | 0.0065 |
| candidate-forced-sort-w1 | combined | 0.0006/0.0010 | 0.7094 | 16.6387/16.6664/16.6684 | 16.6225 | 0.0070 |
| candidate-forced-sort-w1 | clean_frames | 0.0006/0.0010 | 0.7127 | 16.6058/16.6664/16.6680 | 16.5910 | 0.0062 |
| candidate-forced-sort-w1 | edit_frames | 0.0006/0.0010 | 0.7061 | 16.6716/16.6663/16.6687 | 16.6540 | 0.0079 |
| candidate-repair-w1 | combined | 0.0006/0.0012 | 0.0067 | 16.6430/16.6665/16.6686 | 16.6272 | 0.0067 |
| candidate-repair-w1 | clean_frames | 0.0006/0.0012 | 0.0005 | 16.6119/16.6665/16.6680 | 16.5968 | 0.0063 |
| candidate-repair-w1 | edit_frames | 0.0006/0.0010 | 0.0129 | 16.6742/16.6665/16.6689 | 16.6576 | 0.0071 |
| candidate-rebuild-w6 | combined | 0.0006/0.0010 | 0.3677 | 16.6398/16.6663/16.6681 | 16.6229 | 0.0072 |
| candidate-rebuild-w6 | clean_frames | 0.0006/0.0009 | 0.0005 | 16.6134/16.6662/16.6685 | 16.5975 | 0.0066 |
| candidate-rebuild-w6 | edit_frames | 0.0006/0.0010 | 0.7349 | 16.6662/16.6665/16.6675 | 16.6484 | 0.0077 |
