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
| candidate-cached-linear | combined | 0.0004/0.0008 | 0.0002 | 16.6360/16.6666/16.6688 | 16.6251 | 0.0041 |

## Case: sparse-camera-pan

- Validation: single-arm oracle check only (`candidate-cached-linear`: 30/30 frames checked against fixture oracle; inter-arm image parity not applicable with a single arm).
Note: candidate-only `cached-linear` capture (no frozen `--baseline-probe` supplied).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-cached-linear | combined | 0.0446/0.0629 | 0.0014 | 16.6385/16.6669/16.6686 | 16.6294 | 0.0043 |

## Case: dense-z-edit

- Validation: inter-arm image parity and oracle check across 4 arms (candidate-rebuild-w1, candidate-forced-sort-w1, candidate-repair-w1, candidate-rebuild-w6; 30/30 frames matched).

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| candidate-rebuild-w1 | combined | 0.0002/0.0007 | 0.3301 | 16.6361/16.6667/16.6688 | 16.6257 | 0.0044 |
| candidate-rebuild-w1 | clean_frames | 0.0002/0.0006 | 0.0002 | 16.6039/16.6667/16.6686 | 16.5939 | 0.0041 |
| candidate-rebuild-w1 | edit_frames | 0.0002/0.0007 | 0.6599 | 16.6682/16.6668/16.6688 | 16.6575 | 0.0047 |
| candidate-forced-sort-w1 | combined | 0.0005/0.0009 | 0.6837 | 16.6368/16.6666/16.6690 | 16.6234 | 0.0056 |
| candidate-forced-sort-w1 | clean_frames | 0.0005/0.0009 | 0.6813 | 16.5492/16.6665/16.6682 | 16.5368 | 0.0053 |
| candidate-forced-sort-w1 | edit_frames | 0.0005/0.0009 | 0.6862 | 16.7244/16.6666/16.6706 | 16.7101 | 0.0060 |
| candidate-repair-w1 | combined | 0.0004/0.0008 | 0.0050 | 16.6329/16.6667/16.6692 | 16.6212 | 0.0049 |
| candidate-repair-w1 | clean_frames | 0.0004/0.0007 | 0.0003 | 16.5979/16.6667/16.6687 | 16.5868 | 0.0046 |
| candidate-repair-w1 | edit_frames | 0.0004/0.0008 | 0.0098 | 16.6679/16.6668/16.6702 | 16.6557 | 0.0053 |
| candidate-rebuild-w6 | combined | 0.0003/0.0008 | 0.3347 | 16.6359/16.6668/16.7269 | 16.6252 | 0.0047 |
| candidate-rebuild-w6 | clean_frames | 0.0003/0.0008 | 0.0002 | 16.6043/16.6665/16.7961 | 16.5940 | 0.0044 |
| candidate-rebuild-w6 | edit_frames | 0.0003/0.0008 | 0.6692 | 16.6675/16.6669/16.7114 | 16.6563 | 0.0050 |
