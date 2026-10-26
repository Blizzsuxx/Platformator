# ASan/UBSan and leak validation

The sandbox run cannot perform LeakSanitizer thread inspection and is not a valid leak result. The enabled detector was rerun outside that restriction: 11 selected hosts, 3 pass, 8 fail only on leak reports through LoadModule_fluidsynth / FLUIDSYNTH_init / MIX_Init. No memory-access or UB diagnostic was observed. This is not a clean leak check.

To distinguish changes from prior findings, all 57 frozen engine source objects were rebuilt with the same address/undefined instrumentation and current unchanged dependency binaries. Current characterization tests were then compiled against that old core with old private/public headers. Seven baseline overlays confirm identical outcomes and exact leak totals:

| Host | Frozen baseline | Candidate |
| --- | --- | --- |
| platformator_physics_bookkeeping_test | Pass, no diagnostic | Same |
| platformator_physics_support_test | Pass, no diagnostic | Same |
| platformator_runtime_foundation_test | 2200 bytes / 44 allocations | Same |
| platformator_runtime_recreation_test | 400 bytes / 8 allocations | Same |
| platformator_runtime_step_test | 400 bytes / 8 allocations | Same |
| platformator_scene_transition_contract_test | 200 bytes / 4 allocations | Same |
| platformator_transform_hierarchy_test | Pass, no diagnostic | Same |

Raw current XML/logs: bin/runtime-cleanup-validation/asan-unsandboxed.xml and logs/asan-unsandboxed.txt. Fully instrumented baseline source/command/archive provenance: baseline-sanitizer-objects/cleanup-asan/provenance.json; baseline-overlay/cleanup-asan retains test hashes/results/stacks. Existing FluidSynth leaks remain unresolved and no detector suppression or leak-disabled pass was substituted.

Final lookup refinement rechecked in lookup-v2-asan.xml/logs: same outcomes and enabled diagnostic totals; no additional issue observed.
