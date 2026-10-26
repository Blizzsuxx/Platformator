# US1 characterization

Frozen-engine archive overlays and pre-change candidate checks pass: runtime_step, transform_hierarchy, callback_mutation, destruction_contract, queued_deletion_safety and runtime_work. Logs: bin/runtime-cleanup-validation/logs/us1-baseline-tests.txt and us1-prechange-candidate.txt; overlay source hashes/results are retained under baseline-overlay.

New assertions cover paused skipped-dt paths, custom manual advance consumed once, both rendered phase traces, hook failure followed by retirement/phase restoration, and overlapping parent/child updates with retirement cancelling queued work (20 live subtree visits). Existing hierarchy and benchmark-contract hosts already cover the remaining propagation/render-scope contracts; those assertions were retained rather than duplicated.
