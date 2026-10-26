# Final timing captures

Final refined source: candidate-source-freeze-v2.json. All 84 groups have five independent baseline and five candidate captures (840 processes), pinned to recorded CPU sets, 120 warmup/600 measured steps, seed 0, workers 1/2/6. Schema-3 scopes, counters and timing boundaries remain unchanged. All builds/checks ended before captures; the retained schedule alternates matched stages in reproducibly shuffled order.

Raw samples/manifests, logs, source identities, schedule and checksums: bin/runtime-cleanup-validation/final-time-v2. The first implementation's 840-run capture remains under final-time and is historical after the lookup refinement. Additional same-binary controls (480 processes, 10 pairs per group) and persistent-contact follow-ups (240 processes, 20 pairs per group) are retained separately. No oracle/work/sanitizer duration enters these aggregates.

See performance-analysis.md, performance-comparison.csv and per-group-dispositions.json. Nested scopes are not summed; p95/p99/max summaries are means of independent process-level statistics, not pooled tail claims. Startup/shutdown wall time is not labeled complete_step.
