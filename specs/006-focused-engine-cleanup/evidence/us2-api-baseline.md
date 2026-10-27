# API baseline

Current source declares `Collision(const Eigen::Vector2f &normal, float penetration, const ClipPointsWithData &contactPoints, Collider *colliderA, Collider *colliderB)`; penetration is unused. Both animation getters are public and currently return the same current clip; the new round-trip assertion requires an additional baseline-overlay run. Source callers and external consumer implications must be rechecked before changing the signature.

The candidate signature is `Collision(normal, contactPoints, colliderA, colliderB)`; in-repository searches showed no uses of the removed constructor argument. Public callers must update and rebuild/relink. `getCurrentAnimationClip()` remains a forwarding compatibility alias for `getCurrentClip()`.

A test-only overlay of the animation getter equivalence assertion was compiled and run on the frozen debug baseline and passed; the original test file was restored afterward. Output is in `/tmp/platformator-006-baseline/roundtrip-overlay-test.log`.

The equivalence assertion now covers both playing and stopped states, restoring the prior playback state; the updated overlay passed in both frozen baseline and candidate debug builds. Logs: `/tmp/platformator-006-baseline/roundtrip-overlay-test.log` and `getters-candidate-test.log`.
