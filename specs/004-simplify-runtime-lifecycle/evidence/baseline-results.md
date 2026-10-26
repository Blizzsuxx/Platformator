# Baseline results

Current ordinary debug suite: 26 registrations, 24 passed, 2 expected instrumentation skips; package/public-consumer check passed. Work probes at 1/2/6 workers pass, including zero warmed idle allocations, 21 affected-subtree visits and fixed-peak storage reuse. Benchmark-label contracts pass in the Release instrumentation build.

All 84 matrix groups pass invariant validation in baseline-validation-v2; all 84 preliminary single-process timing jobs completed in baseline-preliminary-time. These samples establish cost context, not five-process acceptance. Full logs/JUnit, raw schema-3 samples/manifests, driver schedule, source identity and checksums remain under bin/runtime-cleanup-validation. Fixture attempts before v2 are explicitly excluded.
