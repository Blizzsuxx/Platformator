# Before cleanup: six audit routes

| Journey | Current path | Ownership / validity |
| --- | --- | --- |
| J1 frame | Runtime simulation → GameManager stage lambda → callbacks/physics → transient audio + commit | PhaseGuard restores prior phase; first stage error survives cleanup; render is separate. Two rendered bodies repeat the same flow. |
| J2 mutation | queueReconciliation → reconcilePending → support/event flush → drainRetirements | Pending/draining component lists allow requeueing. Identity/eligibility retire before allocation; callbacks finish before batch deletion. |
| J3 transforms | queueTransform → hierarchy order if dirty → dirty-node sort if needed → synchronizeSubtree | GameObject carries queue slot and stamp. Parent state precedes child publication; workers write local transforms before main-thread publication. |
| J4 sleep | awake reset/integration → pre-step unions → two-pass updateSleepingStates | Rigidbody owns persistent timer and step-local union scratch. Group motion/minimum time is reduced before decisions; traversal membership stays fixed until drain. |
| J5 wake | Rigidbody setters/explicit wake, transform or impact → participation queue → derived lists; support deltas commit before wake | No-op wake preserves timer. New candidates reset scratch after integration. Net support totals distinguish loss from transfer. |
| J6 lifetime | Runtime constructs/publishes two active pointers → pending scene overwrite → prepare/commit → explicit shutdown → reset | Scene sequence has no reader. Runtime/manager both resolve through permitted shutdown callbacks; reset follows cleanup, then pointers clear. |

Baseline code is preserved before production edits; research.md contains original symbol/line anchors. Earlier contact/solver-order amendments remain in force.
