# Candidate physical and ordering validation

All 54 candidate validation-only captures pass their schema-3 reader and physical invariant checks. Recomputed checksums verify every retained artifact. All 27 one/six-worker comparisons and all 54 baseline/candidate contact/body/support snapshot comparisons pass the recorded tolerances. Captures are per-step correctness instrumentation with timing_eligible=false and are not performance data.

Raw artifacts and checksums: bin/contact-events-20261025/candidate/validate/. The unchanged baseline is under baseline/validate/. The internal contact test additionally compares complete generated entry/exit histories for 128 pairs across zero/one/two scripted recipients and one/six workers. A mixed batch gives the entering pair lower handles, then verifies observed delivery: exit:oldA exit:oldB enter:newA enter:newB. This distinguishes Exit-before-Enter ordering from pair-only sorting. See final-order-trace.txt.

The real-runner zero-warmup regression and both existing benchmark contract checks pass (3/3). The fixture builder and measured warmup=120 workloads remain identical; only pre-first-step population validation was corrected.
