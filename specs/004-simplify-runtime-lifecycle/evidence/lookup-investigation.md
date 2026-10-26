# Runtime lookup investigation

Initial paired timings are retained under final-time (840 processes): 48/84 group means increased, with unchanged recorded populations/contact/active-body work in the largest increases. Some large percentages are sub-microsecond quiet steps; active examples include noisy mixed/churn runs and hierarchy publication. No speedup/equivalence claim follows from overlap.

Source audit found that the first single-authority implementation routed privileged lookup through RuntimeAccess::runtime → Runtime::current, adding a checked helper route to a frequent collider/publication path. The refined implementation uses Runtime::tryGetCurrent directly and performs both availability checks together at the existing boundary, preserving the original fast lookup shape and the same error. It still has one authority and no static manager fallback/state. This is also easier to audit locally.

Initial candidate binaries and source are preserved as lookup-v1-bin and lookup-v1-runtimeaccess.cpp. This refinement changes only runtimeaccess.cpp; affected compatibility, work, sanitizer and matrix checks are reopened, and final matched captures will be collected again. The initial measurements remain historical evidence, not a certificate for the refined code.

Final measurement: the lookup-specific 15-process comparison confirms the first route's added cost and the refined route's comparable baseline cost (see lookup-micro-results.json). The full refined matrix, same-binary controls and 20-pair persistent follow-up are analyzed in performance-analysis.md. No broad speedup is attributed to the micro result.
