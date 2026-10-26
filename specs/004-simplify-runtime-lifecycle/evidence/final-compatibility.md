# Final compatibility

Ordinary debug CTest: 26 registrations, 24 pass, two expected instrumentation-only skips. The package/public-consumer check passes. Production configuration builds the engine, runner, public headers and Mario consumer successfully. Final debug/production logs and final-debug.xml are retained under bin/runtime-cleanup-validation.

No public include/platformator header or serialized schema changed. New controlled entry/lifetime/sleep checks and all prior ordinary regressions pass. Release benchmark-label contracts pass separately; work-only tests pass in their instrumented configuration rather than being inferred from skips.

Lookup refinement rechecked: lookup-v2 debug/full production, work 1/2/6 and Release contracts pass. Source freeze v2 differs only in runtimeaccess.cpp; ownership/error/lifetime contracts remain unchanged.
