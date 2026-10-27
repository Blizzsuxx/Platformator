# Collision validation

All six selected candidate collision tests passed, matching the baseline. Full output: `/tmp/platformator-dead-state-0j46vn51/us1-candidate.log`. Controlled benchmark invariant comparisons remain pending.

The full controlled invariant matrix matched for baseline/candidate and across worker counts; four targeted pair-churn traces each matched all 600 candidate/enter/exit samples. The two six-worker rigid-body populations differed in support totals between baseline and candidate, and independently valid uncontrolled rigid-body scenes also showed cross-worker differences under the existing solver-order waiver. See `invariants/comparison.txt` and `baseline-invariants.md`.
