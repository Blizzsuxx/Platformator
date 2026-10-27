# C01–C14 initial source audit

At HEAD `66707f6ba62bda8c795ae34aac4fe45bd07ba9f9`, source, public headers, tests, examples and current benchmark code were searched; historical artifacts were not treated as live consumers. This records initial dispositions, subject to rechecking at removal and final validation.

| ID | Initial disposition and evidence |
| --- | --- |
| C01 | Remove: `pairSlots`/`freePairSlots` are maintained only in `src/grid.cpp`; `pairSlot` is only used to maintain them. Preserve candidate ownership, witness counts and active indices. |
| C02 | Remove: `hierarchyDepth` has only declaration and two writes in `include/platformator/gameobject.h` and `src/gamemanager.cpp`; retain hierarchy order. Installed `GameObject` layout changes: rebuild. |
| C03 | Remove: bias accumulator is initialized/copied and artificially asserted by `tests/regression_tests.cpp`; solver does not read it. Retain true bias and normal/tangent impulses; public layout/source change. |
| C04 | Remove: Audio index is initialized and exposed by accessors but never assigned or read by engine clients; public declarations/layout change. |
| C05 | Remove manager aggregate: only accumulated on loop exit; `SimulationClock::droppedSteps()` remains tested and live. |
| C06 | Remove private parameterized constructor: no caller; default construction and setters are live. |
| C07 | Remove `GameManager::addGameObject` wrapper: no caller; `registerGameObjectSubtree` remains. |
| C08 | Remove uncalled GameObject removal helper and sole downstream manager notifier; retire/reconcile is active. |
| C09 | Remove four uncalled private support accessors; support bits and credits remain live. |
| C10 | Remove `Grid::finishColliderSync`; `queuePairsForCollider` is live. |
| C11 | Remove endpoint setters and single-step witness methods; constructors and batched `addToWitnessCount` remain. |
| C12 | Remove uncalled binding/rebinding chain; two installed public declarations are removed. Binding constructors, projection and cache repair remain live. |
| C13 | Remove `ResolvedAssetPath::requestedPath`; raw input still supplies errors and warnings. Same-named benchmark parameter is unrelated. |
| C14 | Remove six uncalled private direct debug draw methods; active buffered debug rendering stays. |

## Final disposition

All C01–C14 entries above were removed as described. The retained mechanisms and intentionally removed installed declarations are documented in `us1-dispositions.md`, `us2-dispositions.md`, `us3-dispositions.md`, and `docs/CPP_API_MIGRATION.md`. Controlled invariant and work results are recorded under `invariants/` and `work-memory.md`; timing conclusions and limits are in `performance-analysis.md`. Leak checking and graphical overlay equivalence remain unverified, and baseline and candidate both report the SDL audio race under TSan as recorded in `sanitizers.md`.
