# Affected work and retained memory

Matching baseline/candidate cleanup-work controls pass 4/4 selected tests each, plus direct six-worker work execution. Logs: work-baseline.txt and work-candidate.txt. The fixture uses100 warmed unchanged updates and observes zero C++ new/new[] allocations and zero force/movement/sleep/geometry/transition/event/solver-adjacency visits. Script-only replacement causes no physical neighbor refresh. Actual body sleep still refreshes affected solver participation.

Five fixed-peak cycles add sixteen real contacts, replace an actually supported kinematic body and cancel a temporary pending body's lifetime. Each cycle returns original contact membership and exactly one support for the new recipient. Event/contact capacity remains256/512 slots after initial high water. These checks cover membership as well as storage reuse; they do not count every allocator operation or certify all possible retained memory.

Compiled declaration layout: ColliderPair 136→120 bytes (16 fewer bytes/pair on this toolchain), Rigidbody 224→224 bytes. The deleted booleans and padding account for this observed layout; no generic ABI-size promise is made.

Five per-child RSS observations (KiB), identical1000-pair/two-recipient workload and CPU0:
- Baseline: [14284, 14300, 14160, 14156, 14424]; mean 14264.8.
- Candidate: [14232, 14420, 14112, 14272, 14280]; mean 14263.2.

Ranges overlap and means are essentially unchanged: no process RSS improvement is claimed. Reproduction: measure_state.py; exact compile flags/commands and child logs under bin/physics-bookkeeping-20261025/baseline-final-memory and candidate-memory. wait4 measures each runner child's own peak, avoiding inherited shell/compiler high-water marks. Preliminary baseline samples remain separately retained, not overwritten.

Final candidate re-measured after the inline reset fix: pair120 bytes/body224 bytes, RSS KiB [14232, 14324, 14200, 14320, 14236]. Same allocation/storage controls pass; ranges still overlap baseline. Raw final logs/flags: bin/physics-bookkeeping-20261025/candidate-v2-memory.
