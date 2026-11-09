# Contract: Rendering, Resource Preparation, and Idle Work

**Requirements**: FR-001–004, FR-009–011, FR-018–024, FR-034–035, FR-051.

## All-object metadata and image keys

`AuthoringIndex` contains local descriptors and accumulated translation for every scene object; Runtime sprite proxies are an independent ordered renderable subset. It supports marker/collider/camera bounds without instantiating scripts, audio or physics. Unknown/missing data remains editable. Never cache recursive document children inside a proxy.

A Scene render key includes tab/document generation, relevant value/structure/resource revisions, camera/view center and zoom, logical image rect, physical target size and renderer generation. Equal keys reuse a previously rendered image. Changing selection/hover redraws overlays only unless content changes. A relevant content change redraws the full Scene target in authoritative order; a static scene performs zero repeated sprite submissions after settling. A dirty view may use the simple conservative ordered bounds pass; culling cannot change alpha overlap or pick semantics.

Hierarchy topology/visible rows and connector ranges rebuild on structure/expansion changes, labels on affected values, not on every frame. Only visible connector segments/widgets are submitted. Reference validity uses project/path/type/version keys; assignment and Run validate current state rather than blindly trusting display caches. Window title and preferences update only when their source state changes. Preference writes are coalesced at field completion or a bounded deferred boundary and flushed during orderly shutdown.

## Worker/renderer boundary

A request contains project generation, logical/canonical path, content version, requested preview shape and optional document consumer. Metadata probing and checked CPU decode/animation parsing occur in oneTBB tasks. Jobs return immutable prepared pixels/description/error values; stale generations are discarded before upload. Main/host renderer owners alone create/update/destroy SDL textures. No `IMG_LoadTexture` call is permitted from tile drawing or a worker.

Raster admission verifies encoded length ≤64 MiB, dimensions ≤8,192 each, pixel count ≤16,777,216, checked multiplication and supported codec scratch limits before decode. Enforce an inclusive 128 MiB decoder working-set limit, with decoded pixels ≤64 MiB and scratch/input staging ≤64 MiB, using allocator/scratch limits in the narrow vendored-codec bridge; format headers alone do not bound codec scratch. SVG preview initially uses icon fallback: sized raster output alone does not bound XML/parser allocation or complexity. A future admitted SVG path must enforce parser/allocation/work bounds and reject external resources before enabling sized rendering. A format without a safe bounded preparation path yields an icon and a clear preview limitation, without changing its authored reference or ordinary game loader behavior. Thumbnail output is at most 256×256; retain small prepared pixels for renderer replicas, not every full-resolution source.

At most one image decode and 64 deduplicated queued requests exist; visible demand takes priority and obsolete versions cancel. Completed thumbnail pixels total at most 8 MiB. Animation preview descriptions admit at most 1 MiB and 512 frames; oversized documents remain lossless authoring data with explicit preview limits. Never silently truncate saved frames to meet a display budget.

## Cache/reload rules

The thumbnail LRU admits at most 64 MiB and 256 logical entries, accounting retained output pixels plus all renderer-local textures. Count conservative RGBA bytes and documented platform overhead; report separately any unavailable driver-allocation measurement. Frame-pinned visible textures are not freed mid-submission; excess demand defers upload/uses a placeholder until space is safely available. Negative results expire on path version/manual refresh, not on each draw.

The resource dependency index identifies the exact direct and transitive consumers of changed textures/clips. A transactional engine reload prepares a new resource version, validates it, then changes affected proxy/clip references at an idle owner-thread boundary. Old wrappers remain valid until all reference owners release them; cache removal checks resource-version/pointer identity so destroying an old version cannot remove a newer canonical entry. Staging/validation failure keeps last-good data while reporting that current content is unresolved. Existing non-editor runtime cache semantics remain unchanged unless explicit session reload is invoked.

Scene texture/clip loading also uses prepared worker results: do not fall back to synchronous Runtime file decoding from per-frame reconcile. An internal adoption/version bridge binds owner-thread-created prepared textures to proxy wrappers without mutating authored paths or silently returning the old canonical cache entry. Initial authoring texture-version budget is 256 MiB; main Scene target budget is 64 MiB. Admission occurs before allocation/upload; evict only unused/unpinned versions, prioritize visible demand, and use an explicit selectable preview-limit marker if demand cannot fit. Preserve original image dimensions and map source rectangles to prepared texture coordinates so lower-resolution previews do not change authored world geometry. Renderer-local copies and in-flight old/new versions count against their corresponding budgets.

A fresh unrelated asset addition changes only relevant directory contents. Do not destroy browser-wide state or recreate the Runtime. Root/project changes do invalidate the corresponding ownership domains. Keep bounded refresh pending until current dependencies have completed; stale content may display with a label, never as a validated current success.

## Native hosts and frame transfers

Each SDL host owns its renderer/context/font textures. UI images use logical IDs resolved to a replica owned by the current renderer. Moving a panel transfers presentation ownership, not foreign texture pointers. Where a Scene image appears in a secondary host, main Runtime rendering produces the source image and an owner-thread readback occurs only when the image changes. Additional transfer-target/staging/upload buffers and Scene replicas total at most 64 MiB; the original main Scene render target is separately accounted as an authoring resource. Calculate admissible dimensions first and render/blit into a fitting aspect-preserving transfer target before readback; never read an 8K source into an oversized CPU buffer and downscale afterwards. Include source transfer pixels, destination staging and all replicas in admission, and disclose reduced resolution if necessary. Minimized/hidden Scene panels have no image consumer and incur no readback.

Game image pixels follow [play-session.md](play-session.md), with separate two-slot 1.76 MiB transfer accounting and visibility/rate limits. Never claim SDL readback is free or infer GPU elapsed time from CPU API duration. UI host repainting can reuse already-uploaded pixels. Destroy host replicas/font backend before its renderer; invalidate generation before device/window replacement.

## Event pacing

The application waits for input/native completion or the earliest animation, process-output, retry, deferred-persistence or consistency deadline. A native callback/worker completion wakes that wait. No fixed 1 ms busy loop. Active interactions repaint as needed; static targets remain reused even if an animation thumbnail/Console causes UI repaint. When minimized/unfocused, reduce presentation work while still servicing owned processes, watcher events, timers and cancellation. Reentering focus requests current display/semantic state and does not replay stale gestures.

## Acceptance/evidence boundary

Use independent pixel/order/coordinate checks plus counters for changed proxies, submitted sprites, decoded assets, uploads, filesystem display queries, history entries and preference writes. Verify static image reuse, one-property locality, unrelated additions, retained clip references, failed reload, oversize preflight, malformed animation, worker cancellation, two renderer hosts, pin/eviction lifetime and bounded queue exhaustion. A depth-128 sprite-bearing chain must not retain quadratic descendant JSON copies.

Measure the real GUI frame path (input through present), idle process CPU, cold browsing and affected reload latency under [quickstart.md](../quickstart.md). One-/multiworker tests apply only to independent scan/decode throughput; SDL/Runtime/rendering remains serial by design. Rendering-specific comparisons use the existing guarded linear-render driver. Retain raw timings and identities only; pixel checks and transfers stay in memory.
