# Matched GUI performance pilot status

**Review date:** 2026-11-08  
**Outcome:** Unmeasured; SC-007–009 and SC-017 remain unmet.

No GUI capture was run. The baseline probe was compiled before the cleanup changes but never launched; no renderer identity or timing samples were observed. Its executable was later relinked, and the original binary is not retained. A current-only capture would not provide the required matched baseline/candidate comparison, so no one-sided timing is reported.

No raw samples or benchmark artifacts are retained for this pilot. The historical identity record and its limits remain in [BASELINE.md](BASELINE.md). If a behavior-equivalent baseline and candidate can be identified, the next comparison should use one focused displayed-GUI workload and the guarded driver's defaults: 3 repetitions, 5 warmup frames, 30 measured frames, 50 MiB output budget, and 600 seconds total. Do not infer a performance benefit from the historical probe build identity or unrelated correctness runs.
