# Baseline work and memory

Warmed idle control records zero C++ new/new[] allocations, geometry, transition comparisons, events, solver neighbor visits and eligible body work. Existing fixed-peak churn adds sixteen real contacts per cycle and returns original membership over five cycles; event/contact capacities stabilize at 256/512. See baseline-source/bin/cleanup-work/Testing/Temporary/LastTest.log and baseline-work-tests.txt.

Compiled declaration sizes: ColliderPair 136 bytes, Rigidbody 224 bytes. Five fresh runner child RSS observations (KiB): [14324, 14300, 14300, 14200, 14300]. Workload: 1,000 persistent pairs, two scripted endpoints, one worker, CPU0, warmup120/measure600. Exact flags, command, logs and methodology: bin/physics-bookkeeping-20261025/baseline-memory/measurements.json. measure_state.py uses wait4 for each child, excluding inherited shell/compiler peaks. Neither observation proves overall allocator behavior or future memory savings.
