# Candidate results

Completed and checksum-verified 270 Release timing processes: 27 cases × workers 1/6 × five independent runs; 120 warmup and 600 measured steps, seed 0, dt 1/120. Affinity CPU0 for one worker and CPUs0,2,4,6,8,10 for six. No deliberate builds/tests ran concurrently. Sanitizers, work probes and debug tools were disabled. Capture log ends COMPLETED.

Raw samples, summaries, manifests, logs, aggregate.csv and checksums.json: `bin/contact-events-20261025/candidate/time/`. Separate validation directory has 54 complete schema-3 captures, all passing physical/worker comparison. These ignored raw directories must be retained or archived before cleaning bin/.

The analysis verifies all 540 baseline/candidate timing runs and matching workload/build/hardware metadata. See [performance analysis](performance-analysis.md), [all 54 complete-step comparisons](performance-table.md), [mean/p95/p99/spread CSV](performance-comparison.csv), and [nested-scope/run data](performance-data.json). Both schema versions remain explicitly labeled; old event counters are not relabeled as generated transitions.

Candidate source fingerprint: `8d53dcc4b0591b09275130f12be38484f2c9274f8c18ace208d7fb8603276f94`.
Baseline source fingerprint: `ccd078ffe9b17f26ed79486cda8a5b809b0737a5219daf64f1e98b0a20e5f07c`.
