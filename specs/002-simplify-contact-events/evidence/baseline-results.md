# Baseline results

All 270 timing processes completed: 27 cases × workers 1/6 × five independent repetitions; 120 warmup and 600 measured steps, seed 0, dt 1/120. Affinity CPU 0 for one worker and 0,2,4,6,8,10 for six, CPU governor performance. Build Release without sanitizer/work/debug instrumentation. Raw data, manifests, logs, aggregate and SHA-256 inventory: `bin/contact-events-20261025/baseline/time/`. Validation-only data retained separately.

Baseline warmed idle work test passes with zero allocations; see baseline-work.txt. Peak child RSS for 1000 observed persistent pairs: 14164 KiB, measured with Python resource.getrusage (the host has no /usr/bin/time). The event vector previously omitted listener-free contact events, so increased initial record storage there is an intentional correctness cost, not equivalent counter semantics.
