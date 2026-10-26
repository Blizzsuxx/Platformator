# Final work and retained storage

The final work host passes with workers 1/2/6 on candidate and frozen-engine overlays. Logs: bin/runtime-cleanup-validation/logs/final-work-tests.txt, final-baseline-work-overlay.txt and final-baseline-work-{2,6}.txt.

Over 100 warmed idle steps, counted C++ allocations and unrelated hierarchy/transform/reconciliation/geometry/contact-event/solver-refresh/force/movement/sleep work remain zero. Repeated root edits visit the original 21-object subtree once; overlapping parent/child work with one retired child visits 20 live objects. Terminal bodies are excluded before integration. Script-only replacement does not refresh physical adjacency. Fixed-peak contact/replacement churn restores populations and reuses event/contact capacities; scene/recreation correctness tests also restore lifetime ownership without stale work.

These are operator-new and retained-capacity observations, not a claim about all C/SDL allocations or process RSS. Existing enabled sanitizer leak diagnostics are reported separately; no leak-freedom claim follows from stable vector capacities.

Lookup refinement rechecked: lookup-v2 debug/full production, work 1/2/6 and Release contracts pass. Source freeze v2 differs only in runtimeaccess.cpp; ownership/error/lifetime contracts remain unchanged.
