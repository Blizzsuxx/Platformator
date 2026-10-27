# Sanitizer attempts

Both frozen baseline and candidate were built with ASan+UBSan and TSan. Complete test diagnostics are retained in `sanitizer-logs/diagnostics.tar.xzdata`.

- With `ASAN_OPTIONS=detect_leaks=1` and UBSan halt-on-error, all nine selected tests failed on both builds because LeakSanitizer terminated with a fatal error and a ptrace-related hint. Leak checking is unverified; no leak-free claim follows.
- A separate diagnostic retry with `detect_leaks=0` passed all nine selected tests on both builds with UBSan halt-on-error. This is not a substitute for the leak-enabled run.
- The full TSan physics/mutation/lifecycle label selection produced identical outcome counts on both builds: five passed, ten failed and the work-probe-only test skipped. The failures report a race in SDL audio playback/stream format initialization, with engine initialization in the stack. `halt_on_error=1` stops each affected test at the first race, so other races in those tests cannot be excluded. No suppression was used.
