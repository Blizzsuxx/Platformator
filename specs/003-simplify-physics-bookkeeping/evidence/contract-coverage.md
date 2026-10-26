# Contract coverage

Existing tests were reviewed rather than duplicated. New gaps are addressed by physics_bookkeeping_test and support additions, characterized against baseline before their corresponding refactor.

| Contract / journeys | Actual checks |
| --- | --- |
| J1–J3 body motion/type/kinematic/gravity/sleep | physics_bookkeeping_test::bodyParticipation, regression_tests sleeping/support stacks; one/six workers |
| J4 support accounting/net cancellation | physics_support_test::supports (128 real supports), referenceSwaps and netSupportTransfer |
| J5 old/new recipients before free | physics_support_test::replacementBeforeFree / scriptsPreserveSleepingSupport, bookkeeping pending retirement; work fixture fixed-peak replacement |
| J6 queue/contact lifecycle | physics_bookkeeping_test::queueLifecycle (131 queued→128 after cancellation); contact_events_test::transitions |
| J7 filter/trigger/activity/multiple witnesses | physics_support_test::supports/stationary, collider_pair_cleanup_test::runMultiWitnessTransitions |
| J8 callback, scene, exceptions and deferred deletion | contact_events_test::retirementDuringEnter/eligibilityAtDelivery/cleanupFailure/mutationAndFailure; callback_mutation, queued_deletion_safety, scene_transition_stress/contract, destruction_contract hosts |
| Independent generated histories, recipients0/1/2, workers1/6 | contact_events_test::transitions captures generated records separately from endpoint delivery |
| Quiet100 and true re-contact/no replay | contact_events_test::quiet/lateScript and transition controls; expanded runtime_work_test 100 warmed steps |
| Mixed Exit-before-Enter and A/B order | contact_events_test::mixedTransitionOrder, generated histories and contact fixture canonical endpoints |
| Shape math, manifold/features/warm start | physics_geometry_test, regression_tests warm_start_feature_reuse/clip_segment_feature_propagation and geometry-comparison.json |
| Schema3 eight scopes/eleven counters and strict current consumers | benchmark_contract_test, benchmark_persistent_zero_warmup, benchmark_sweep_test / Python suite; no reporting producer/reader change |
| Public API/scene/package compatibility | independent public headers, package_smoke installed/relocated SDK and Mario consumers; scene IO/roundtrip/runtime hosts |

US3 execution output is recorded separately; this matrix identifies the actual assertions and is not a claim that all final configurations already ran. SC-001 needs a real maintainer walkthrough despite this automated coverage.
