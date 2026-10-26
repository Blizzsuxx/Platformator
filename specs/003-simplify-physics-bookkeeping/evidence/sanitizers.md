# Sanitizer validation

ASan/UBSan build passes with instrumented dependencies. Executed outside the process sandbox to permit LeakSanitizer thread inspection, based on the restriction independently demonstrated in feature002; this run did not disable/suppress leaks for its leak-enabled checks.

- Changed physics-only hosts with leaks enabled: 5/5 pass (bookkeeping, contact, support, geometry, transform). Includes old/new support recipient observation at the real before-free boundary, net-zero transfers and one/six-worker queue/body paths.
- Full non-package leak-enabled suite:25 registrations,9 pass,14 fail,2 instrumentation skips. Fourteen leak summaries total8800 bytes/176 allocations, matching the prior FluidSynth initialization finding. Reported leaked allocation stacks pass through LoadModule_fluidsynth/MIX_Init. No memory-access or UB diagnostic was observed in that run. This is not a clean leak check and no leak fix/suppression is claimed.
- Separate full non-package memory-access/UB check with ASAN_OPTIONS=detect_leaks=0:25 registrations,23 pass,2 instrumentation skips, no access/UB errors. This result does not establish leak freedom.

Commands/output and JUnit files: asan-core.txt/xml, asan-full-leaks.txt/xml and asan-access.txt/xml; exit status summary asan-status.json. Unchanged desktop/backend attribution is contextual, not a universal safety certificate. Prior engine-free FluidSynth probe is retained in feature001 evidence; no unrelated dependency rewrite or detector suppression was added here. Package consumers are covered normally, not certified as sanitized by package smoke's independent rebuild.

TSan build succeeds. Outside-sandbox physics/mutation/lifecycle selection:16 registrations,13 pass,2 fail,1 work-instrumentation skip. The two failures are scene_transition_contract_test and runtime_step_test, with lock-order-inversion/potential-deadlock stacks through SDL_Init/SDL_Quit and system libdbus. Detailed second_deadlock_stack output is retained in tsan-selection.txt/xml. No data-race warning occurred in the selection; changed bookkeeping/contact/support/geometry, callback mutation, scene stress, destruction, queued deletion and worker tests pass.

These are the same two desktop test names and stack paths as feature002's documented findings. Backend source is unchanged. Prior engine-free SDL probe did not reproduce the cycle, so exclusive dependency attribution remains unproven. The failures remain unresolved; no TSan suppression or deadlock-detector disabling was applied. This does not certify full-engine race/deadlock freedom.

## Final candidate recheck after hot-path helper fix

All sanitizer selections were repeated on the rebuilt final candidate: leak-enabled core5/5; full leak check9 pass/14 fail/2 skip with8800 bytes/176 allocations; separate memory-access/UB23 pass/2 skip; TSan13 pass/2same desktop lock-order failures/1 skip, no data-race warnings. Original commands, full output and JUnit: v2-asan-*.txt/xml, v2-tsan-selection.txt/xml, v2-sanitizer-status.json and v2-check-counts.json. No suppressions added and the prior limitations still apply.
