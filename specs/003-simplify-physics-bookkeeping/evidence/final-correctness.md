# Final normal/production/benchmark checks

Full cleanup-debug build and normal CTest complete: 26 registered, 24 passed, two expected instrumentation-disabled skips (runtime_work and benchmark_contract). Installed and relocated SDK consumers, stock/source consumers and Mario package smoke pass. Production incremental rebuild completes all76 steps, including runner, Mario, test hosts and independent public-header checks. Benchmark configuration: 3/3 CTest checks pass, including real zero-warmup runner; independent Python unittest discovery runs10 cases successfully.

Original commands/output: final-debug-checks.txt, final-production-checks.txt and final-benchmark-checks.txt. No public authoring operation, scene format or schema-3 producer/consumer was removed or changed. Work and sanitizer/physical/timing evidence are separate. A trailing empty line at collision.cpp EOF was removed after compilation; it changes no code token. Release metadata is refreshed before final physical/timing captures.

## Final candidate after reset-helper fix

The final private-header inline definition was rebuilt in all six configurations. Full Debug/installed/relocated package suite repeats successfully:26 registered,24 passed,2 expected instrumentation skips (v2-full-debug.txt/xml). Final Production and benchmark builds/checks pass; all four work selections and direct six-worker control pass. Original v1 checks remain retained rather than silently relabeled.
