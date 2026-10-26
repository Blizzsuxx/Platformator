# Baseline physical captures

All 36 validation groups completed with per-step independent oracle enabled; all 18 one/six-worker comparisons pass existing tolerances. All raw checksums verified. Manifest source fingerprint: `5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031`. Raw summaries/samples/manifests/logs/checksums: `bin/physics-bookkeeping-20261025/captures/baseline/validate/`. These are not timing-eligible results. Common CMake/test scaffold is retained in common-test-scaffold.patch; no baseline runtime behavior was changed. Final Release timing is deferred until both immutable runners and all competing checks are ready.

## Final matched timing

Completed180 independent Release processes for this stage (36 groups×5),120 warmup/600 measured,seed 0,dt 1/120; schema 3, fixed matching affinity and immutable runner/library tree. Baseline/candidate jobs were matched and interleaved in recorded shuffle-seed1 order,360 processes total. No competing deliberate builds/tests/probes. Strict readers and all raw checksums verified. Raw files, manifests, commands, schedules, aggregates and checksums: bin/physics-bookkeeping-20261025/captures/baseline/time. Source fingerprint: `5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031`. See performance-table.md, performance-comparison.csv and performance-data.json for all scopes/tails/run spread; regression interpretation and focused follow-ups are recorded separately.

## Final candidate-v2 matched matrix

Both stages completed180 new independent timing processes,360 total, under captures-v2/baseline/time. All raw checksums, actual source fingerprints, schema 3 and matched metadata verified. Final baseline5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031; final candidate b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db. The original matrix above remains an initial-candidate observation. See performance-analysis.md for final tables, all regressions,60 final follow-up processes and the inline-helper fix.
