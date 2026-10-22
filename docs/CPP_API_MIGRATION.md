# C++ cleanup migration

Include public headers as `<platformator/runtime.h>` etc.; link `Platformator::platformator`. Private `src` forwarding headers are removed. C++20 and transitive bundled dependencies are exported by the target. The installed SDK includes fallback assets independently of Mario.

## Scenes and callbacks

`Runtime::loadScene(path)` now **replaces** the single scene. At idle it prepares and commits synchronously. From ordinary script/animation/collision callbacks it queues a replacement and returns; use `requestLoadScene(path)` / `requestClearScene()` to make this explicit. Requests share one last-wins slot latched once at the end of the step. No recursive simulation occurs. Requests emitted by retirement exits belong to the next step. `clearScene()` leaves an empty scene, including no camera; a loaded camera-less scene gets a fallback camera.

Preparation validates the whole graph, IDs, component slots, references and resources before retiring live objects. Failed preparation preserves the live scene and current path. Deferred errors surface at the host step boundary; catch them around `simulateFrame`, not around a request inside a script. Ordinary retirements already requested by scripts still drain on failure. Saving is idle-only and writes an exclusive sibling temporary file before replacing the destination; failures preserve the prior file/path.

`Scene::loadScene()` returns borrowed roots owned by the current runtime. Ownerless JSON construction of GameObjects is rejected. Use runtime creation/loading. Hierarchy links describe relationships; the runtime owns objects in one flat collection. Destroying a subtree retires every object once, regardless of allocation order.

New behaviors start once before their first ordinary callback, on the next eligible step. Destroying/disabling an object or replacing/removing its component suppresses later callbacks immediately; physical removal and allocation release happen at the barrier. Off/on before the barrier reconciles the final desired state. Do not retain pointers to replaced components.

`onDestroy` is read-only: required attached components remain allocated during the callback batch. Creating, destroying, reparenting, loading/clearing, saving or attaching/removing components there throws. No further simulation callbacks run on retired objects. Surviving collision recipients may read the retired other object during the final exit callback; both-retired pairs receive no callbacks. Runtime shutdown finishes these callbacks before releasing assets/backends.

## References and serialization

`ObjectReference<T>` stores a typed runtime handle (slot, generation, runtime serial), not a cached pointer. `get()` returns null after logical retirement, slot reuse, scene replacement or runtime recreation. Check optional references at the use boundary; dereference operators assert a live target. Raw pointers returned by runtime/component getters are borrowed and become invalid after retirement cleanup. File IDs are scene-local serialization identities, never runtime lifetime identities; registered IDs cannot be changed.

Handwritten behavior resolvers must change their signature and forward the explicit resolver:

```cpp
void resolveReferences(platformator::ReferenceResolver &resolver) override
{
    resolver.resolve(target);
    resolver.resolve(texture);
}
```

Include `<platformator/referenceresolver.h>`. Registration/serialization macros already pass `resolver`; regenerate handwritten equivalents accordingly. A required non-null missing/wrong-kind reference rejects preparation with context. An expired bound handle never resolves to another object with the same file ID. Saving an unbound non-null ID or a live reference outside the saved graph rejects serialization; expired bound references serialize as null.

Asset references retain resources; replacement retains the new resource before releasing the old one. Keep asset references within their Runtime lifetime. Required scene resources fail load; optional missing resources diagnose and use packaged fallbacks.

## Steps, transforms and startup

Explicit `simulateFrame(dt)` executes one ordered step with finite positive caller dt. Interactive `run()` uses 1/120-second fixed steps, at most eight per render iteration, retains fractional time and counts dropped ticks. Pause clears accumulated time; manual advance performs one tick. Physics workers integrate their own local transforms, then the main thread publishes parent-first world caches. Public world-position reads remain current after parent edits; callers need no manual collider synchronization.

Set `RuntimeOptions::assetRoot` or `--asset-root`. Relative explicit roots resolve once against startup cwd. Default roots are executable/assets then executable-parent/assets. Paths preserve case, normalize separators and reject escaping the root. Supported legacy scene-relative and animation-relative paths resolve through their explicit context; no upward/demo-directory search remains. Scene paths can be absolute or startup-relative.

`windowSettings.headless = true` / `--headless` skips video/audio device initialization. Desktop startup errors throw (CLI exits nonzero). `--smoke-steps N` runs a finite positive number of explicit ticks. Linux/GCC is the validated environment; other SDL-supported platforms need their own validation.

Benchmark CSV is schema v2: `complete_step` includes cleanup, while rendering is separate. Legacy partial `frame` data is not equivalent. See [benchmark documentation](../benchmark/README.md) and [validation evidence](../specs/001-cpp-engine-cleanup/validation.md).
