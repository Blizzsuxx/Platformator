# Baseline correctness

cleanup-debug build and full CTest passed: 24 registered checks, 22 passed and 2 explicitly skipped (work probes/benchmark instrumentation disabled). Package smoke built, installed and relocated independent public consumers and Mario. Eight Python benchmark tests passed.

All 54 validation-only captures passed: 27 cases at workers 1 and 6, including persistent contacts with zero/one/two scripts. The existing compare routine verified exact identity/support/sleep/feature fields and tolerances: positions/velocities/separation 1e-4; rotations/angular velocities/normals 1e-5. Raw files: `bin/contact-events-20261025/baseline/validate/`, including per-run manifests, command logs and checksums.json.
