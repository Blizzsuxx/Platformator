# Render Order & Culling Pilot Report

- Repetitions: 3, Warmup: 5, Measured: 30
- Baseline binary: `build/render-order-baseline/platformator_linear_render_probe`
- Candidate timing: `build/render-order-timing/platformator_linear_render_probe`
- Candidate validation: `build/render-order-validation/platformator_linear_render_probe`

- Renderer mapping: backend=`software`, vsync=`1`, output=320x240

## Case: authored-static

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| baseline-indexed | combined | unavailable | unavailable (frozen baseline) | 16.6635/16.6666/16.6778 | 16.6467 | 0.0058 |
| candidate-indexed | combined | 0.0007/0.0010 | 0.0005 | 16.6385/16.6665/16.6737 | 16.6215 | 0.0055 |
| candidate-cached-linear | combined | 0.0005/0.0008 | 0.0004 | 16.6276/16.6664/16.6925 | 16.6116 | 0.0054 |

## Case: sparse-camera-pan

| Arm | Slice | Selection Median/p95 (ms) | Order Prep Mean (ms) | Paired Mean/Median/p95 (ms) | Render Mean (ms) | Step Mean (ms) |
|---|---|---:|---:|---:|---:|---:|
| baseline-indexed | combined | unavailable | unavailable (frozen baseline) | 16.6639/16.6665/16.6713 | 16.6439 | 0.0088 |
| candidate-indexed | combined | 0.0294/0.0394 | 0.0029 | 16.6363/16.6665/16.6677 | 16.6177 | 0.0084 |
| candidate-cached-linear | combined | 0.0681/0.0907 | 0.0025 | 16.6217/16.6666/16.6821 | 16.6029 | 0.0084 |

## Verification, Identities & Scope (T077)

- Command: `python3 engine/benchmarks/capture_linear_render.py --comparison render-order-culling --focused-cases authored-static,sparse-camera-pan --baseline-probe build/render-order-baseline/platformator_linear_render_probe --candidate-validation-probe build/render-order-validation/platformator_linear_render_probe --candidate-probe build/render-order-timing/platformator_linear_render_probe --asset-root engine/benchmarks/assets --workers 1 --multiworker-count 4 --seed 0 --output specs/019-sprite-z-ordering/evidence/pre-removal-grid-vs-cached-20261118`
- Build identities:
  - Frozen baseline (`build/render-order-baseline`): `Release`, `work_probes=false`, `fixture_sha256=170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`, `harness_sha256=c1345c9ec9de4e08d1942b904cd293d1d930b73fa384bd49990001a048f1d62a`, `source_sha256=ae1f6003dc402c7243fb91d9cb91e161c86b9b376a73b3572fbe8881759c021e`.
  - Candidate timing (`build/render-order-timing`): `Release`, `work_probes=false`, `fixture_sha256=170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`, `harness_sha256=99056ad405221df2795293581720971a1bc845ddb526f94ac43367a45cbe1d61`, `source_sha256=11673d3f9bd612cd5b4c5d783c874f0b8af74381cb7d3f4caeb0de1e65764319`.
  - Candidate validation (`build/render-order-validation`): identical `Release`, `fixture_sha256`, `harness_sha256`, `source_sha256` with `work_probes=true`.
- Host & mapping: Linux 6.12.112-1-MANJARO `x86_64`, AMD Ryzen 5 7535HS (12 logical CPUs), `workers=1`, `renderer=software`, `vsync=1`, `output=320x240`, 0 frame fallbacks.
- Parity: `authored-static` (30/30 frames) and `sparse-camera-pan` (30/30 frames) matched on submissions and final RGBA FNV-64 hashes between `candidate-indexed` and `candidate-cached-linear`.
- Retained artifacts: `report.json`, `report.md`, `proofs/`, `resources/`, `raw/`, `logs/` (840 KiB total); raw timing CSVs/manifests and 31 exact commands are retained in `raw/` and `report.json`.

## Candidate Comparison (`candidate-cached-linear` vs `candidate-indexed`)

### 1. Timing (Selection & Paired Step+Render, same timing binary)

- **`authored-static` (64 sprites, fixed camera)**:
  - `visible_selection_ms` (`SDLWindow::prepareVisibleSelection` CPU time):
    - `candidate-indexed`: median `0.0007 ms`, p95 `0.0010 ms`, mean `0.0009 ms`, run-to-run spread `[0.0007, 0.0012] ms` (`stdev=0.0003 ms`).
    - `candidate-cached-linear`: median `0.0005 ms`, p95 `0.0008 ms`, mean `0.0006 ms`, run-to-run spread `[0.0005, 0.0006] ms` (`stdev=0.0001 ms`).
    - Delta (`candidate-cached-linear - candidate-indexed`): mean `-0.0003 ms`, median `-0.0002 ms`, range `[-0.0007, -0.0001] ms` (`stdev=0.0003 ms`). Both pre-removal arms hit the clean O(1) cache path every measured frame (`30` cache hits, `0` queries/visits), within ~100–300 ns measurement jitter.
  - `paired_step_render_ms` (wall time including software present & `vsync=1`):
    - `candidate-indexed`: median `16.6665 ms`, p95 `16.6737 ms`, mean `16.6385 ms`, run-to-run spread `[16.6374, 16.6392] ms` (`stdev=0.0009 ms`).
    - `candidate-cached-linear`: median `16.6664 ms`, p95 `16.6925 ms`, mean `16.6276 ms`, run-to-run spread `[16.6199, 16.6324] ms` (`stdev=0.0067 ms`).
    - Delta (`candidate-cached-linear - candidate-indexed`): mean `-0.0109 ms`, median `-0.0084 ms`, range `[-0.0193, -0.0050] ms` (`stdev=0.0075 ms`), within ~16.67 ms 60 Hz VSync-paced noise; does not exceed VSync pacing variation.

- **`sparse-camera-pan` (10,000 sprites, continuous camera pan)**:
  - `visible_selection_ms` (`SDLWindow::prepareVisibleSelection` CPU time):
    - `candidate-indexed`: median `0.0294 ms`, p95 `0.0394 ms`, mean `0.0327 ms`, run-to-run spread `[0.0307, 0.0365] ms` (`stdev=0.0033 ms`).
    - `candidate-cached-linear`: median `0.0681 ms`, p95 `0.0907 ms`, mean `0.0707 ms`, run-to-run spread `[0.0695, 0.0726] ms` (`stdev=0.0016 ms`).
    - Delta (`candidate-cached-linear - candidate-indexed`): mean `+0.0380 ms`, median `+0.0388 ms`, range `[+0.0361, +0.0391] ms` (`stdev=0.0017 ms`; `2.17x` selection ratio). This **does exceed run-to-run variation** (`+0.0380 ms` vs `stdev <= 0.0033 ms`), directly measuring the difference between spatial-grid candidate scanning (`5,952` candidate visits over 30 frames) and linear full-population scanning (`300,000` candidate visits over 30 frames) in `prepareVisibleSelection`.
  - `paired_step_render_ms` (wall time including software present & `vsync=1`):
    - `candidate-indexed`: median `16.6665 ms`, p95 `16.6677 ms`, mean `16.6363 ms`, run-to-run spread `[16.6335, 16.6392] ms` (`stdev=0.0029 ms`).
    - `candidate-cached-linear`: median `16.6666 ms`, p95 `16.6821 ms`, mean `16.6217 ms`, run-to-run spread `[16.5922, 16.6397] ms` (`stdev=0.0258 ms`).
    - Delta (`candidate-cached-linear - candidate-indexed`): mean `-0.0146 ms`, median `-0.0059 ms`, range `[-0.0413, +0.0036] ms` (`stdev=0.0236 ms`). Inside full frame wall time, the `+38 µs` (`0.038 ms`) selection cost is submerged beneath ~`16.67 ms` software VSync frame pacing and does not exceed run-to-run variation.

### 2. Work Counters, Cold Registration & Retained Bytes (Validation Probe)

- **`authored-static` (64 sprites, 30 measured frames)**:
  - Candidate visits: `0` (`indexed`), `0` (`cached-linear`); `cache_hits=30` on both, `0` warm allocations.
  - Cold construction + registration: `10.699 ms` / `1,061` alloc calls / `808,990 B` (`indexed`) vs `11.355 ms` / `769` alloc calls / `751,798 B` (`cached-linear`).
  - Retained culling state (`peak_owned_bytes`): `41,992 B` (`6,656 B` common + `35,336 B` grid) for `indexed` vs `6,656 B` (`0 B` grid) for `cached-linear` (**6.3x smaller**, `-35,336 B`).
- **`sparse-camera-pan` (10,000 sprites, 30 measured frames)**:
  - Candidate visits: `5,952` (`indexed`, with `30` grid queries and `124` cell visits) vs `300,000` (`cached-linear`, `300,000` `full_population_visits`), `0` warm allocations on both.
  - Cold construction + registration: `34.357 ms` / `145,971` alloc calls / `28,748,982 B` (`indexed`) vs `20.965 ms` / `80,343` alloc calls / `13,781,838 B` (`cached-linear`) — **grid removal cuts cold setup time by `-39%` (`-13.39 ms`), cold allocation calls by `-45%` (`-65,628`), and cold allocated bytes by `-52%` (`-14.97 MB`)**.
  - Retained culling state (`peak_owned_bytes`): `10,762,072 B` (`1,703,936 B` common + `9,058,136 B` grid) for `indexed` vs `1,703,936 B` (`0 B` grid) for `cached-linear` (**`-84%` / `-9.06 MB` retained memory**).

### 3. Work-Only Controls (Separated, Non-Timing Build)

- **`rejected-z-edit` (10,000 sprites)**: Both `indexed` and `cached-linear` observed `3,000` rejected-only Z edits (`0` pose edits, `0` `sprite_order_sorts`, `0` queries, `0` warm allocations).
- **`fully-moving-crossing` (10,000 sprites, 30 frames)**: Both `indexed` and `cached-linear` observed `300,000` pose edits, `300,000` `bounds_refreshes`, and `300,000` `candidate_visits` (`current-linear` uncached control: `0` culling work, `300,000` pose edits). Only `indexed` incurred extra grid maintenance (`35,440` `index_insertions`, `20,186` `index_relinks`, `35,436` `index_removals`) and `9,058,136 B` grid memory (`10,762,072 B` total peak vs `1,703,936 B` cached-linear).

## Conclusion: Cost of Grid Removal by Workload (Pre-Removal `candidate-cached-linear` vs `candidate-indexed`)

- On **fixed-camera workloads (`authored-static`, 64 registered sprites in both pre-removal arms)**, both `candidate-cached-linear` and `candidate-indexed` hit the O(1) clean cache every measured frame (`cache_hits=30`, `0` queries/visits). Observed timing deltas (`candidate-cached-linear - candidate-indexed`) stay within run-to-run variation: `visible_selection_ms` delta is `-0.0003 ms` mean / `-0.0002 ms` median (range `[-0.0007, -0.0001] ms`, `stdev=0.0003 ms`; `0.0005 ms` vs `0.0007 ms` median) and `paired_step_render_ms` delta is `-0.0109 ms` mean / `-0.0084 ms` median (range `[-0.0193, -0.0050] ms`, `stdev=0.0075 ms` under `16.67 ms` 60 Hz software VSync pacing; `candidate-cached-linear` stdev `0.0067 ms` vs `candidate-indexed` stdev `0.0009 ms`). Grid removal saves `35,336 B` (`35.3 KB`) retained culling memory and `292` C++ allocation calls during construction and registration (`769` vs `1,061` calls, `751,798 B` vs `808,990 B`), with `11.355 ms` vs `10.699 ms` cold setup in the validation run.
- On **continuous 10,000-sprite camera pans (`sparse-camera-pan`, 10,000 registered sprites in both pre-removal fixtures)**, `candidate-cached-linear` increases `prepareVisibleSelection` CPU time relative to `candidate-indexed` from `0.0294 ms` median (`0.0394 ms` p95, `0.0327 ms` mean, spread `[0.0307, 0.0365] ms`, `stdev=0.0033 ms`, `5,952` candidate visits) to `0.0681 ms` median (`0.0907 ms` p95, `0.0707 ms` mean, spread `[0.0695, 0.0726] ms`, `stdev=0.0016 ms`, `300,000` candidate visits) — a `+0.0380 ms` mean (`+0.0388 ms` median, range `[+0.0361, +0.0391] ms`, `stdev=0.0017 ms`) selection-phase cost that exceeds run-to-run variation. In paired step+render wall time (`vsync=1` software renderer), the delta is `-0.0146 ms` mean / `-0.0059 ms` median (range `[-0.0413, +0.0036] ms`, `stdev=0.0236 ms`), submerged within 60 Hz VSync pacing jitter. In exchange, for the same `10,000` registered sprites, grid removal eliminates `9,058,136 B` (`9.06 MB`) retained grid memory, `13.39 ms` cold construction/registration time (`20.965 ms` vs `34.357 ms`), `65,628` C++ allocation calls during construction and registration (`80,343` vs `145,971` calls), `14.97 MB` cold allocated bytes (`13,781,838 B` vs `28,748,982 B`), and `91,062` dynamic grid operations (`35,440` insertions, `20,186` relinks, `35,436` removals) observed in the separate `10,000`-sprite `fully-moving-crossing` work-only control.

