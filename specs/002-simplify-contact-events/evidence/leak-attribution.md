# Leak-enabled validation

LeakSanitizer cannot inspect threads inside the tool sandbox (ptrace diagnostic); the same contact test passes outside it with leak detection enabled. The full non-package ASan/UBSan suite outside the sandbox registers 24 tests: eight pass, fourteen fail on leak reports, two instrumentation checks skip. All 140 reported allocation stacks include LoadModule_fluidsynth and MIX_Init; total 8800 bytes / 176 allocations. No engine-owned allocation stack or ASan access/UB diagnostic was observed in this run.

This reproduces the already attributed external dynamic-initialization issue recorded in specs/001-cpp-engine-cleanup/validation.md, sections “LeakSanitizer access resolved” and “Full LeakSanitizer run and dependency attribution”, with its independent engine-free mixer probe. It is an actual outstanding dependency leak, not a sanitizer pass or a false positive. Physics-only transform/support/contact/geometry hosts pass with leaks enabled. No suppression, MIDI removal or dependency change is introduced by this feature.

The separate detect_leaks=0 run verifies memory access and UB for the complete non-package suite; it does not certify leak freedom. Original failed output and the successful leak-enabled contact output are retained beside this report.
