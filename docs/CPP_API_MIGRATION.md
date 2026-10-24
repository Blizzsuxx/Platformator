# C++ cleanup migration

Include public headers as `<platformator/runtime.h>` etc.; link `Platformator::platformator`. Private `src` forwarding headers are removed. C++20 and transitive bundled dependencies are exported by the target. The installed SDK includes fallback assets independently of Mario.

## Scenes and callbacks

`Runtime::loadScene(path)` now **replaces** the single scene. At idle it prepares and commits synchronously. From ordinary script/animation/collision callbacks it queues a replacement and returns; use `requestLoadScene(path)` / `requestClearScene()` to make this explicit. Requests share one last-wins slot latched once at the end of the step. No recursive simulation occurs. Requests emitted by retirement exits belong to the next step. `clearScene()` leaves an empty scene, including no camera; a loaded camera-less scene gets a fallback camera.

Preparation validates the graph, IDs, component slots, behavior deserialization and component resources before retiring live objects. Failed preparation preserves the live scene and current path. Behavior references and behavior asset fields resolve only immediately before that behavior's first `start()`, against the published scene. Loaded and programmatic behaviors use this same startup path; inactive behaviors wait for activation. A behavior resolution error is logged after replacement has committed: the old scene is not restored, start is skipped for that attempt, and the behavior remains runnable. Other callback and engine errors can still escape `simulateFrame`. Ordinary retirements already requested by scripts still drain on failure. Saving is idle-only and writes an exclusive sibling temporary file before replacing the destination; failures preserve the prior file/path.

`Scene::loadScene()` returns borrowed roots owned by the current runtime. Ownerless JSON construction of GameObjects is rejected. Use runtime creation/loading. Hierarchy links describe relationships; the runtime owns objects in one flat collection. Destroying a subtree retires every object once, regardless of allocation order.

New behaviors remain pending until initialization on the next eligible step. After one initialization attempt, including a logged failure, they join fixed/update/late/collision dispatch. Additions to an already active ScriptComponent follow the same rule. Startup drains pending behaviors directly, so eligible behaviors added during startup can initialize in the same pass. Additions during fixed/update/late/collision dispatch wait for the next startup pass. `getBehaviors()` returns runnable behaviors; `getPendingBehaviors()` returns those awaiting startup as `const std::vector<Behavior *> &`. `getBehaviors()` retains its vector return type. `getBehavior<T>()`, serialization and component teardown inspect both owning collections. There is no third ownership/runnable list. The global startup queue holds script components, with one queue index per component. Destroying/disabling an object or replacing/removing its component suppresses later callbacks immediately; physical removal and allocation release happen at the barrier. Off/on before the barrier reconciles the final desired state. Do not retain pointers to replaced components.

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

Include `<platformator/referenceresolver.h>`. Registration/serialization macros already pass `resolver`; regenerate handwritten equivalents accordingly. A required non-null missing/wrong-kind reference fails at behavior startup, before `start()` runs. Each pending behavior gets one initialization attempt and then joins runnable dispatch, even if initialization failed. Resolver/start exceptions are logged per behavior and do not escape startup or destroy the behavior. A resolver exception skips start for that attempt. Reactivation does not retry either operation. Scripts are responsible for handling partially initialized state in ordinary callbacks. Attach a new instance or reload the scene for a fresh initialization attempt. Behavior assets use the same `assets/...` paths as component and animation assets and remain required resources. An expired bound handle never resolves to another object with the same file ID. Saving an unbound non-null ID or a live reference outside the saved graph rejects serialization; expired bound references serialize as null. Loaded behavior references are unbound before startup, so saving a newly loaded scene with such references can fail until they have been resolved.

Asset references retain resources; replacement retains the new resource before releasing the old one. Keep asset references within their Runtime lifetime. Required scene resources fail load; optional missing resources diagnose and use packaged fallbacks.

## Steps, transforms and startup

Explicit `simulateFrame(dt)` executes one ordered step with finite positive caller dt. Interactive `run()` uses 1/120-second fixed steps, at most eight per render iteration, retains fractional time and counts dropped ticks. Pause clears accumulated time; manual advance performs one tick. Physics workers integrate their own local transforms, then the main thread publishes parent-first world caches. Public world-position reads remain current after parent edits; callers need no manual collider synchronization.

Set `RuntimeOptions::assetRoot` or `--asset-root`. Relative explicit roots resolve once against startup cwd. Default roots are executable/assets then executable-parent/assets. All nonempty asset paths must start with the literal `assets/` prefix, for example `assets/textures/Player.PNG`. That maps to `<assetRoot>/textures/Player.PNG`; the configured directory need not itself be named assets. Paths preserve case and normalize backslashes to forward slashes. Bare `textures/...`, scene/animation-relative paths, absolute asset filenames, parent traversal (`..`) and symlinks escaping the configured root are rejected even for optional loads. Missing files with a valid assets path can still use an intentional optional fallback. Replace legacy spellings with explicit assets paths; no automatic scene-relative migration remains in the engine or editor. The checked-in engine/Mario scenes and animations already use this form. Scene-opening paths remain a separate host API supporting absolute, startup-relative, or logical `assets/...` paths.

`windowSettings.headless = true` / `--headless` skips video/audio device initialization. Desktop startup errors throw (CLI exits nonzero). `--smoke-steps N` runs a finite positive number of explicit ticks. Linux/GCC is the validated environment; other SDL-supported platforms need their own validation.

Benchmark CSV is schema v2: `complete_step` includes cleanup, while rendering is separate. Legacy partial `frame` data is not equivalent. See [benchmark documentation](../benchmark/README.md) and [validation evidence](../specs/001-cpp-engine-cleanup/validation.md).
