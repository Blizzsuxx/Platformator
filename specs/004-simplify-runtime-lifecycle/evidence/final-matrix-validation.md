# Final paired matrix validation

All 84 groups pass in both baseline and candidate (168 independent correctness processes). This covers all six controlled probe modes, small/large populations, dense/uniform spatial guards, churn/migration and persistent contacts with zero/one/two recipients at workers 1/2/6. Controlled populations/connectivity/destruction results and resolved parameters match across stages; existing runner checks independently validate physical/support state. Unconstrained solved trajectories are not compared for equality.

Common probe source is byte-identical across stages. A fresh baseline-final-source carries current test/build-only overlays while src/include/cmake match the frozen original hashes. The unchanged frozen benchmark core/binaries are reused rather than silently rebased. Its recorded recorder/build fingerprint remains the original baseline identity; CMake's later test-only INTERNAL flag affects no measured engine option. Final candidate source hashes still match candidate-source-freeze.json.

Raw paired outputs, source fingerprints, fixture parameters, job schedule and checksums: bin/runtime-cleanup-validation/final-validation. No per-step oracle durations enter timing aggregates. Prior enabled sanitizer/backend limitations remain separately reported.

The direct checked lookup refinement also passes all 168 paired correctness processes in final-validation-v2; final source freeze v2 is verified.
