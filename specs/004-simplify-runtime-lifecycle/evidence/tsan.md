# TSan validation

Outside the sandbox: 16 registrations, 13 pass, 2 fail with SDL/libdbus lock-order-inversion/potential-deadlock reports, 1 work-instrumentation skip. No data-race report occurred in that run. Both failing host names match prior feature findings; the warnings remain unresolved, not passes.

A fully instrumented frozen core was also rebuilt (57 old engine objects, old headers, unchanged dependency binaries). The current bookkeeping and runtime-foundation overlays pass. The current runtime-step and scene-transition-contract overlays reproduce the same two lock-order warnings on that old core. This supports distinguishing the reports from this cleanup, without claiming exclusive dependency attribution or universal deadlock freedom.

The initial sandbox run additionally reported SDL audio races. They did not reproduce outside that restriction, so the sandbox-only reports are retained separately rather than silently suppressed or accepted as clean evidence. No detector was disabled.

Raw candidate logs/XML: bin/runtime-cleanup-validation/tsan-unsandboxed.xml and logs/tsan-unsandboxed.txt. Baseline archive/source provenance: baseline-sanitizer-objects/cleanup-tsan/provenance.json; baseline-overlay/cleanup-tsan retains individual test hashes, logs and statuses.

Final lookup refinement rechecked in lookup-v2-tsan.xml/logs: same outcomes and enabled diagnostic totals; no additional issue observed.
