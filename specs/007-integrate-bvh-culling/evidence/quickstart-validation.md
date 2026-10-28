# Current quickstart verification

The quickstart interfaces were exercised on source `f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1` after the final code changes. Current evidence replaces earlier source-42 completion claims; historical raw records remain intact.

- Build and SDK usage: full Release CTest passed, including installed/relocated/source consumers and public headers. Defaults remain segmented/none; Box2D stays private.
- Selection and failure cases: CLI/runtime contracts check independent choices, all-eight combinations, unknown tokens, invalid padding/bounds, inactive headless/no-camera rendering and clear failures.
- Geometry/lifetime/output: current image/order/lifecycle fixtures cover transforms, source/animation changes, removal/reactivation and camera/view mutations. Actual case validation additionally compares same-frame submit-all pixels under the recorded 1,600×900 software renderer. [Current images](convergence/render-images/) are retained.
- Incremental edge cases: [verify_edge_cases.py](convergence/verify_edge_cases.py) and [its commands/captures](convergence/verification/edge-cases/) verify uniform zoom without sprite-bound invalidation and exact retained multi-cell transitions at one/six workers.
- Replay, engine and cold startup commands: [capture.py](convergence/capture.py) and per-suite `*-command.txt` files retain the executed generator/sweep/runner flags, matched validation directories and immutable case/profile/source keys. The [coverage ledger](convergence/coverage.csv) includes every required case.
- Timing/resource separation: five-process timing is paired to matching validation; resource captures use the separate interceptor binary and matching timing directory. Source flags and the [binary audit](convergence/verification/binary-hooks-final.txt) verify separation.
- Output contracts and integrity: strict schema-4/sweep tests pass in [full CTest](convergence/verification/release-ctest-final.log); every current capture bundle passes [checksum/identity/qualification verification](convergence/INTEGRITY.json).

The guide's actual renderer examples use software/offscreen/dummy audio here. Hardware renderer behavior/performance remains unverified. Resource/validation durations and first-frame startup data are never mixed into steady-state timing comparisons.
