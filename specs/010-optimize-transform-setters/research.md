# Research: transform reads and redundant setters

## Position caching and mutation

**Decision**: Evaluate the existing eager immediate-position cache as a provisional candidate against the recursive baseline and feasible alternatives. Keep the separate frame snapshot.

**Rationale**: Baseline reads recursively add local positions at O(depth); candidate reads are O(1) but an ordinary ancestor edit refreshes O(subtree) and costs an extra vector per object. Frequent nested edits can repeat work and must be measured. Immediate public reads exclude deferring all publication until the next frame.

**Alternatives considered**: Uncached recursion is simplest and has cheap local edits but fails the stated repeated-read goal; use it as the baseline and reconsider the requirement if evidence does not justify a cache. Lazy/versioned caches may reduce eager updates but introduce invalidation, hierarchy traversal or synchronization costs; concurrent collision reads make unsynchronized mutable lazy writes unsafe. A more complex concurrent or batched design needs evidence before adoption. Parallel subtree publication adds scheduling and overlapping-write concerns; no reason to add it without measurement.

## Physics synchronization

**Decision**: Preserve per-body parallel local integration, join, then use the existing ordered transform synchronization for batch publication; ordinary public edits publish immediately on the main thread.

**Rationale**: Parent and child may move in the same batch. Workers own distinct local state; post-join ordered publication ensures descendants reflect both moves before collision callbacks or other observable reads. Validate lifetime, ordering and worker counts; do not infer thread safety from performance results.

**Alternatives considered**: Eager subtree writes from each worker can race and overlap. Forcing a full hierarchy update after each worker move is expensive. A separately synchronized lazy cache adds complexity.

## Same-value semantics

**Decision**: Validate first; skip unchanged dimension, offset, mass, friction, restitution and gravity updates. Keep wake checks before velocity/angular velocity no-op checks; preserve force/torque and explicit sleep/wake semantics. Repeating body type intentionally reinitializes state and invalidates response.

**Rationale**: Shape edits schedule geometry, dimensions also update inertia; mass/material/gravity setters wake or queue bodies. Repeating body type has observable reset behavior. Exact equality is consistent with existing transform/filter guards; no epsilon-based suppression of real changes.

**Alternatives considered**: Guarding every setter uniformly breaks reset/wake semantics. Leaving all updates unconditional retains avoidable work. A shared abstraction adds indirection for a few local guards.

## Evidence

**Decision**: Use a bounded matched baseline/candidate pilot and an independent work-count/correctness pass, supplemented by complete-step controls. Record raw samples, identities, settings and limitations. Stop at 600 seconds or 50 MiB output.

**Rationale**: Existing broad-phase and rendering workloads alone do not exercise hierarchy reads or setter patterns. Microbenchmarks identify local effects but cannot prove a complete-engine improvement. Three short repetitions provide exploratory evidence only. The current baseline revision and provisional changes are identifiable, but no before/after timings have yet been collected.

**Alternatives considered**: Full historical campaigns are explicitly disallowed without an explicit larger request and would exceed the desired scope. Retaining executables, source archives, per-frame images or build trees in reports is unnecessary and contrary to the artifact policy.
