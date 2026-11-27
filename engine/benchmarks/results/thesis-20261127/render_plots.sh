#!/usr/bin/env bash
set -euo pipefail

OUT="engine/benchmarks/results/thesis-20261127"
CROSS="engine/benchmarks/results/cross-engine-20261127"
PYTHON="${PYTHON:-python3}"

mkdir -p "$OUT/plots/en" "$OUT/plots/sr" "$CROSS/plots/en" "$CROSS/plots/sr"

for lang in en sr; do
  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/broad_phase_w6.csv" --compare "$OUT/broad_phase_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "N,NlogN,N2" --x-tick-step 500 \
    --output "$OUT/plots/$lang/01_broad_phase.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/broad_phase_stress_w6.csv" --compare "$OUT/broad_phase_stress_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "N,NlogN,N2" --x-tick-step 50 \
    --output "$OUT/plots/$lang/02_broad_phase_stress.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/narrow_phase_w6.csv" --compare "$OUT/narrow_phase_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "N,NlogN,N2" --x-tick-step 25 \
    --output "$OUT/plots/$lang/03_narrow_phase.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/rigid_body_container_w6.csv" --compare "$OUT/rigid_body_container_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "N,NlogN,N2" --x-tick-step 250 \
    --output "$OUT/plots/$lang/04_rigid_body_container.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/add_and_remove_w6.csv" --compare "$OUT/add_and_remove_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "N,NlogN" --x-tick-step 100 \
    --output "$OUT/plots/$lang/05_add_and_remove.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/moving_add_and_remove_w6.csv" --compare "$OUT/moving_add_and_remove_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "1,N,NlogN" --x-tick-step 100 \
    --output "$OUT/plots/$lang/06_moving_add_and_remove.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --input "$OUT/ideal_world_w6.csv" --compare "$OUT/ideal_world_w1.csv" \
    --two-panel --thesis-style --dpi 300 --lang "$lang" \
    --fit-slope --budget-lines --hide-zero-series --merge-deferred-grid --include-cleanup \
    --references "N,NlogN,N2" --x-tick-step 2500 \
    --output "$OUT/plots/$lang/07_ideal_world.png"

  "$PYTHON" engine/benchmarks/plot_scaling.py \
    --render-report engine/benchmarks/results/thesis-20261121/render-culling/report.json \
    --thesis-style --dpi 300 --lang "$lang" \
    --output "$OUT/plots/$lang/08_render_culling.png"
done

"$PYTHON" engine/benchmarks/plot_cross_engine.py \
  --input "$CROSS/cross_engine_comparison.json" \
  --output-dir "$CROSS/plots"
