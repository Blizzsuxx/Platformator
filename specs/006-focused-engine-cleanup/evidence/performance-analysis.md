# Final performance and memory analysis

## Asset requests

For the matched, warmed isolated texture workload ([asset-timing.md](asset-timing.md)), 1,000 repeated retained requests averaged 10.788 ms baseline versus 0.0246 ms candidate. Alternate validated spellings were similar. These measure a specific in-process loop, not a complete game frame. Separate interposition observed 198 additional stat/lstat calls and 99 realpath calls when increasing the baseline from one to 100 measured hits, while candidate intercepted-call totals did not rise. Other syscall categories were not exhaustively observed.

The feature adds alias bookkeeping on load and eviction. A shuffled ten-process-per-version follow-up (`timings/asset-churn-followup.json`, seed 16, CPU 0, two warmup cycles) measured 100 complete load/release cycles at 4.759 ± 0.184 ms baseline and 5.172 ± 0.221 ms candidate; 1,000 cycles averaged 46.357 ± 1.354 ms versus 50.566 ± 1.948 ms. The latter is approximately 9.1% slower for this isolated workload. It includes loading, decoding and release, so it does not isolate eviction alone; workloads may differ. Single per-process aggregate loop samples do not support a reliable p95.

`work-memory.md` contains allocation interposition and a 300-alias retained-memory probe. The process-level live-byte differential was approximately 181 KB for unusually long path spellings; this is not a universal per-asset overhead and after-release totals include allocator/library effects. The cache has a deliberate live-alias storage and churn cost in exchange for avoiding repeat resolution.

## Engine and debug controls

[engine-analysis.md](engine-analysis.md) records comparable one/six-worker complete-step averages, p95s, nested scopes and run variation across lanes, dense, moving churn and persistent-contact workloads. All matching physical-invariant snapshots were equal. Two initial one-worker differences did not reproduce in a focused repeat. Captures used 20/60 warmup/measured steps, shorter than the repository example, so tail conclusions are limited. These results do not establish a general engine speedup or regression.

A separate debug growth work fixture observed 1,023 copied/replaced vertex buffers in the baseline versus 1,023 preserved during moves in the candidate for 1,000 objects; see `work-memory.md`. That establishes selected work reduction, not a timing or rendering result. A real X11 renderer was unavailable, and debug-on Release rendering timing and visual equivalence remain unverified. Leak-enabled sanitizers and affected TSan paths also have documented limitations in `sanitizers.md`.

## Phase 7 convergence follow-up

After exception safety and capacity reclamation, 180 matched shuffled Release processes (ten per version/cell, CPU 0) compared the pre-feature baseline, prior implementation and final candidate; raw samples and binary hashes are in `convergence/asset-timings.json`. For 1,000 retained hits the mean ± sample standard deviation was 10.7884 ± 0.8230 ms baseline, 0.0255 ± 0.0057 ms prior and 0.0223 ± 0.0002 ms final. For 1,000 load/release cycles it was 44.9049 ± 1.0432 ms baseline, 51.6460 ± 7.2153 ms prior and 48.9249 ± 1.7405 ms final. The final remains roughly 9% slower than the pre-feature baseline in this isolated churn workload; the prior run was noisy, so it does not establish a precise improvement from capacity reclamation. These are operation-level loops, not whole-engine speed claims.

The separate high-alias-count eviction control measured 1.673 ± 0.062 ms prior versus 1.799 ± 0.108 ms final for 50 rounds of 300 removals. It captures the cost of occasional geometric rehashing; it does not represent ordinary single-alias churn. `work-memory.md` records direct bucket-count, allocator and memory limitations.
