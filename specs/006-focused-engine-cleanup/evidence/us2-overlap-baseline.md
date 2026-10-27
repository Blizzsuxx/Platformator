# Overlap baseline

Frozen debug and production `platformator_collider_pair_cleanup_test`, `platformator_physics_geometry_test` and `platformator_regression_tests` passed. Pair cleanup exercises multiple cell witnesses, final removal/re-entry and filtering with one and six workers. Coverage for endpoint ties, large intervals, fast motion and split/merge requires a focused audit; do not infer complete coverage solely from the passing suite.

`tests/regression_tests.cpp` covers fast-motion checkpoint cancellation, cross-chunk motion, long bridge intervals and split/merge repair. Existing pair cleanup exercises multicell first/final witness and re-entry at one/six workers. Added an equal-endpoint overlap control that migrates away and returns; both the frozen baseline overlay and candidate passed at one/six workers (`ties-baseline-test.log`, `ties-candidate-test.log`). These checks do not exhaust every possible arrangement or solver trajectory.
