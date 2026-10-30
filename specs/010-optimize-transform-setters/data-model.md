# Data Model

## Game object transform

- **Authoritative state**: local translation and optional parent; children form an acyclic owned hierarchy.
- **Derived state**: current public world translation, plus the separate world translation published at the normal runtime synchronization boundary. The provisional implementation stores both; a replacement must preserve their observable meanings.
- **Invariant**: current world translation is the local translation plus the parent's current world translation, or local translation at a root. After any public edit or reparent/detach, an immediate read observes the new hierarchy; reparenting preserves world translation subject to existing floating-point behavior.
- **Transitions**: main-thread edit/reparent updates immediately; worker motion writes each body's local translation and becomes publicly observable after workers join and ordered synchronization completes. Destruction and scene changes must not leave references to retired objects.
- **Validation**: finite translations, acyclic hierarchy, existing runtime ownership and mutation rules.

## Collider

- **State**: shape dimensions, offset, geometry/response/filter dirty status and membership in physics/grid.
- **Transitions**: changed dimensions/offset schedule required geometry work; unchanged validated assignments do not. Dimensions are positive and offset is finite. Trigger and filter behavior remains unchanged.

## Body

- **State**: body type, sleep state, motion, forces, mass, inertia, material, gravity and participation membership.
- **Transitions**: changed physical properties retain existing wake and membership effects. Identical mass/material/gravity assignments are no-ops. Velocity and angular velocity check waking thresholds before skipping equality; force/torque retain existing wake behavior. Body-type assignment remains reinitialization even for the same type. Explicit sleep/wake preserve existing semantics.
- **Validation**: finite vectors and scalars; positive dynamic mass, nonnegative other mass/friction/restitution and restitution no greater than one.

## Measurement record

- **Fields**: baseline/candidate source and diff identities, fixture version and parameters, compiler/build flags, machine and worker settings, commands, boundaries, repetitions and raw timings, plus separate work/resource observations and failures.
- **Rules**: equivalent workload/build settings and independent runs; reports must distinguish direct operations, complete-step and nested scopes. Retain compact records only, never build trees, executable bundles, image dumps or source archives.
