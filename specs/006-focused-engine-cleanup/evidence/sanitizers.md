# Sanitizer results

Both frozen baseline and candidate were built with `cleanup-asan` and `cleanup-tsan`; raw test diagnostics are in `sanitizer-logs/diagnostics.tar.xzdata`.

- Leak-enabled ASan/UBSan: all seven selected tests failed in both trees because LeakSanitizer reported a fatal ptrace-related environment limitation. Leak checking remains unverified.
- ASan/UBSan diagnostic with `detect_leaks=0`: all seven selected tests passed in both trees. This is not a leak check.
- TSan physics/mutation/lifecycle selection: the baseline reported nine failed tests and the candidate eight; each had one work-only skip, with remaining tests passing. All affected test failures stopped on SDL audio stream/playback races (SDL audio/mixer stacks); both versions show the same class of report. Different counts reflect which tests happened to encounter the race. Since `halt_on_error=1` stops at the first report, other races in affected tests cannot be excluded. No suppression was used.

After adding the focused asset, scene and membership tests, the candidate leak-disabled ASan/UBSan run was rebuilt and rerun: all five selected hosts passed (`candidate-final-asan-test.log` in the archive). The leak-enabled and TSan results above predate those test-only additions; engine code was unchanged, but the new cases have not been independently covered by leak-enabled or TSan runs.

After removing the unused internal PathManager parameters, the final candidate ASan/UBSan build and the five focused hosts were rerun: all five pass with leak detection disabled. A new leak-enabled attempt again failed because of the LeakSanitizer environment restriction. The final TSan selection again reported SDL audio races; raw final logs are included in the archive. These outcomes remain limitations rather than leak-free or race-free claims.
