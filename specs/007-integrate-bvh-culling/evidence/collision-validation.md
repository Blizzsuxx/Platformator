> Historical pre-convergence record. Current implementation, qualification and completion evidence are in [convergence/IMPLEMENTATION.md](convergence/IMPLEMENTATION.md), [final-validation.md](final-validation.md) and [RESULTS.md](RESULTS.md). Earlier raw data/source identities remain unchanged.

# Collision broad-phase validation (T029)

## Environment and commands

Validated on Linux 6.18.49-1-MANJARO x86_64 with GCC 16.2.1, using the probes-enabled
`bin/cleanup-work` build. The fixture exercises headless physics, so no renderer or GPU is involved.
The six-worker allowance was available to oneTBB. These are correctness runs, not timing samples.

```sh
cmake --build bin/cleanup-work --target platformator_collision_broad_phase_test \
  platformator_spatial_physics_integration_test --parallel 4
ctest --test-dir bin/cleanup-work -R \
  'platformator_collision_broad_phase_test|platformator_spatial_physics_integration_test' \
  --output-on-failure
```

## Matched exact-overlap oracle

`platformator_collision_broad_phase_test` compares the segmented context and dynamic tree against
an independent all-pairs exact AABB/filter oracle after every publication. Controlled inputs cover
negative coordinates, a point, touching edges, a rotated rectangle, a large multi-cell rectangle,
bit-63/bit-2 symmetric filters, inactive activation/deactivation, asymmetric-filter rejection,
coalesced updates under padding, slot-generation reuse and movement/resize. The randomized run uses
seed `0x5a17`, 72 initial endpoints and 160 publication steps; each step mutates 1–12 endpoints
through bounds, filters, activation and occasional retirement/reuse. Dynamic-tree padding is 8 world
units; the segmented reference uses 32-unit cells.

Result: 169 exact snapshots (9 controlled and 160 randomized), 197 exact-pair observations, 27
candidate additions and 26 removals. Every snapshot matched the independent oracle in both methods;
the two backends produced identical canonical transition sets. The test's ordered-input FNV-1a-64
fingerprint is `7e53f5d8200506e7`; its canonical state/candidate/transition snapshot fingerprint is
`992b3ff213fd9835`. The stdout SHA-256 is
`5b88982a60fff8aadd51b72a1a419a8e110f67234ce8e253f703bf2da4eaea3c`; the tested source SHA-256 is
`9d673e68c8cdb9b993c25fb8eeeae55bb9b9303fd9cec39c5cb90fbcd6bb0e8d`.

## Headless engine integration

The real-runtime integration matrix ran segmented/serial/padding 0, BVH/serial/padding 0, and
BVH/parallel/padding 4. Each mode ran with one and six allowed workers. It checked persistent solid
contacts and support, trigger response without fabricated edges, symmetric filter removal and
restoration, body replacement, off-screen movement out and back, and deterministic final state.
BVH padding also included a near-miss whose padded envelopes overlap; exact refinement kept it out
of contact pairs and callback events. Callback-time destruction delivered one Enter and one Exit
before reclamation, unchanged persistent trigger contacts retained one pair and exactly two Enter
callbacks over 16 idle frames, and scene replacement/clear drained the selected backend.

Serial and six-worker event/checkpoint traces matched within each mode. Final body positions were
required to agree within `1e-4` world units. All assertions passed. The integration stdout SHA-256
is `f4b3b100446791c7e167f0b0804b03c042aa4ae61b19bb955f71d1912e84d729`; the tested source SHA-256
is `225d3ade992c17ff1d3d60450f2d75de64c9db5bc321cf9cc3490f73322b900e`.

The acceptance limits here are the tested distributions and headless runtime fixtures. They do not
claim hardware-renderer behavior, a performance winner, or sanitizer coverage; those belong to
later validation tasks.
