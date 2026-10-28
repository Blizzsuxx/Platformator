# User Story 3 acceptance

The corrected source is `f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1`. The [results](RESULTS.md), [coverage ledger](convergence/coverage.csv) and [integrity record](convergence/INTEGRITY.json) link every reported case to retained eligible data.

| Evidence | Validation | Timing | Resources |
|---|---:|---:|---:|
| Separate calibration | 80 | 400 | — |
| 41 replay cases | 248 | 1,240 | 248 |
| 31 actual-engine cases | 176 | 880 | 176 |
| 20 matched seed-0 physics controls | 40 | 200 | 40 |
| 31 cold first-frame variants | 176 | 880 | — |

All entries are eligible; each timing configuration has five fresh processes. Resource and validation durations are timing-ineligible. Current totals are 720 validation, 3,600 timing and 464 resource processes. The 127-row coverage ledger includes calibration, primary cases, controls and cold variants, without claiming an exhaustive Cartesian product.

Collision replay drives the production prepared Grid/TBB or PhysicsSpatialIndex path and common pair state. Explicit-coordinate/mask references validate final candidates and exact net transitions. Visibility replay shares incremental live preparation and charges changed bounds, required index work, refinement and ordered publication. Routine metrics do not scan tree quality; zero-change replay avoids full snapshot reconstruction. Dense work probes bound adjacency queue visits by dirty endpoint degrees.

The actual-engine matrix includes all prescribed physics families/counts, rendered 0/1/10/100% visibility, moving/pan/jump/resize/animation/churn companions, and all-eight combined correctness. Workers 1/6 are retained. Each rendered validation frame is compared with an ordered submit-all redraw using exact RGBA32 pixels, with matching source/case/SDL/backend/output/presentation proof. Main rendered cases validate 720 frames; cold cases validate one first frame. Physical-only validation cannot qualify rendered timing. No readback or reference drawing enters timing.

Timing scopes are direct wall intervals with disjoint query/reduction phases; parallel worker durations are not summed. Initialization is reported as runtime/fixture setup plus separately measured cold first-step/render publication. Owner capacity estimates, tracked allocation views and process RSS are separate, with coverage/exclusions explicit. Same-method controls match legacy resolved parameters and common timing boundaries; observed residual differences and near-no-op timer overhead remain reported.

The evidence supports reproducible selection, including negative results; it does not require speedup or default adoption. Hardware rendering, pinned-core timing, external contention monitoring and a large-batch query crossover remain unverified. The bounded live matrix is complete; 10,000-proxy coverage is replay-based. Historical raw captures were neither overwritten nor relabeled.
