# Runtime Contract

## Object position and hierarchy

- `getPosition()`, `getX()` and `getY()` report current world translation immediately following supported public position setters, parent movement, `addChild` or `removeChild`, without a simulation step.
- Reparent/detach preserve world translation under existing arithmetic semantics; later changes to the new ancestors affect descendants. Invalid values, ownership violations and cycles retain existing rejection behavior.
- Runtime rendering and physics continue to consume their frame-synchronized snapshot. Parent/child body movement is visible after the worker batch joins and the existing synchronization phase completes.

## Collider and body assignments

- Validate arguments even on an unchanged assignment. Unchanged width, height, radius and offset do not schedule geometry or refresh inertia. Changed values preserve their previous effects.
- Unchanged mass, friction, restitution and gravity do not wake a body or schedule participation/inertia updates. Changed values preserve existing effects.
- Velocity and angular velocity still apply the existing threshold-based wake check before an unchanged assignment returns. Force and torque retain their existing threshold-based behavior.
- Reassigning body type retains initialization, participation and collision-response effects. Non-dynamic reinitialization clears force/torque; static reinitialization also clears motion. Explicit sleep and wake semantics remain unchanged.

These semantics intentionally define the previously ambiguous same-value behavior; callers that relied on waking through a repeated material or gravity assignment must use explicit `wakeUp()`.

## Performance evidence status

Correctness and affected-work checks pass for the targeted cases. The exploratory pilot shows large gains for deep reads and unchanged setters, with lower medians for setter-heavy complete steps. Plain complete steps and physics scopes were slightly slower in this small fixture. These results support targeted workload improvements, not an overall engine or multicore speedup; see the [compact results](../evidence/RESULTS.md).
