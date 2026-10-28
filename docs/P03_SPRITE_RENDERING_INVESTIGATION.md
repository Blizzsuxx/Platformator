# P03 — Sprite rendering investigation

Investigated 28 October 2026 at `ad9b4f38188dd7e54ba6ceb46263dfe22f349b75`. Runtime implementation is unchanged. Probe sources, raw measurements and verification records are under [evidence/p03](evidence/p03).

**A separate sprite-and-camera AABB structure is viable.** In this codebase that means reusing the segmented sweep-and-prune implementation, optionally with its grid partitioning. The camera contributes a query rectangle; only camera/sprite overlaps need to be retained. A BVH also uses AABBs, but organizes them differently.

**Recommendation for P03:** prepare the camera once and add conservative sprite culling first. A linear scan is inexpensive at the current example's scale (20 sprites). Cache render bounds where maintenance is justified. For a large, mostly static level, the bundled Eigen BVH and a lightweight render grid both merit consideration; the existing sweep-and-prune machinery does not win merely because it already exists. Reusing it requires separating its interval mechanics from its current pair-storage interfaces. The measurements below do not establish the best index for a population of moving sprites.

The [current sprite loop](../src/sdlwindow.cpp#L273) calls [Camera::render](../src/camera.cpp#L30) for every active sprite. Camera scale is computed on every call. The vendored SDL [unrotated path](../vendored/SDL3-3.4.18/src/render/SDL_render.c#L4084) and [rotated path](../vendored/SDL3-3.4.18/src/render/SDL_render.c#L4237) still prepare/queue work without an engine-level visibility test. Moving the rejection ahead of these calls can save substantial work.

The separate-AABB proposal was tested directly in [spatial_probe.cpp](evidence/p03/spatial_probe.cpp). It uses an independent `AABB` and, separately, an independent `Grid`, with detached bounds adapters and an independent object registry. No proxy is attached to a GameObject's component slots or the runtime's physics manager. Adapters are needed because the current interval API is typed in terms of `Collider*`. Only camera/sprite pairs pass the masks. No narrow phase, contact generation, solver or gameplay callbacks run.

There are two relevant properties of the current implementation:

- [Projection repair](../src/segmentedintervallist.cpp#L190) walks neighboring endpoints and swaps until sorted. This exploits small coherent changes, but a camera jump across a populated level crosses many endpoints. An unchanged camera still has cheap repair checks.
- [Insertion/removal](../src/segmentedintervallist.cpp#L469) scans intervals and checkpoints to discover affected pairs. The [AABB handler](../src/aabb.cpp#L102) checks the other axis before applying group/mask filtering. Suppressing sprite/sprite results therefore does not suppress all the work of encountering them. The [existing grid](../src/grid.cpp#L356) confines this work to cells, but retains cell queues, pair witnesses, adjacency and pair-delta reduction.

Removing and reinserting the camera avoids swapping its endpoints across the entire intervening world. It still enumerates candidates and updates checkpoints/pair state. This was measured as a separate variant rather than assumed to be free. A rendering specialization could instead update a direct visible-sprite set and filter camera/sprite interactions earlier. Its performance has not been measured here.

Spatial-query measurements used 100,000 static rectangles, 16×24 units, at 32-unit spacing in 400 columns. The camera is 1280×720; approximately 943 rectangles overlap it. Results include producing the visible IDs and restoring sprite order. Times are **microseconds per query**, medians of five batches, with one TBB worker:

| Camera behavior | Cached linear scan | Separate existing SAP | SAP, camera reinserted | Separate existing grid + SAP | Bundled Eigen BVH |
| --- | ---: | ---: | ---: | ---: | ---: |
| Stationary | 59.1 | 10.4 | 2570.4 | 17.7 | 10.9 |
| Pan 2 units/query | 57.5 | 19.2 | 2562.4 | 18.6 | 13.4 |
| Alternate between x=0 and x=8000 | 70.8 | 28607.4 | 5265.0 | 768.1 | 15.2 |

Initial construction in this layout was approximately **3449 ms** for one global SAP, **155 ms** for the partitioned existing grid/SAP, and **13.9 ms** for the Eigen BVH. These are single construction samples, including the adapters and current pair machinery for SAP/grid. They are not timings of an optimized, generic render-only SAP implementation. The global SAP is deliberately distinguished from the grid-partitioned implementation used by physics.

At 10,000 rectangles, the cached scan took approximately 6–8 microseconds/query, and the BVH 10–13 microseconds. For these smaller populations, an index can cost more than scanning compact cached bounds. Any index can also reuse an unchanged visible set when both view and scene are unchanged; the table does not implement that optimization for the scan or BVH.

An independent [rendering probe](evidence/p03/probe.cpp) compared the actual current `Camera::render` with prepared camera data, a computed-bounds cull, a cached-bounds scan, a simple static grid and the bundled BVH. All render variants retain the same submission order. Times below are **milliseconds per software-renderer frame**, including clear, drawing and present:

| Sprites | Visible | Rotation | Current | Compute bounds + scan | Cached bounds + scan | Static render grid | Static BVH |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1% | None | 0.744 | 0.102 | 0.083 | 0.080 | 0.081 |
| 10,000 | 1% | None | 6.682 | 0.356 | 0.172 | 0.167 | 0.180 |
| 10,000 | 1% | Mixed | 14.935 | 0.445 | 0.247 | 0.234 | 0.240 |
| 10,000 | 100% | None | 7.398 | 7.228 | 7.225 | 7.533 | 7.787 |
| 100,000 | 1% | None | 64.260 | 3.209 | 0.793 | 0.746 | 0.701 |
| 100,000 | 1% | Mixed | 148.067 | 4.032 | 1.673 | 1.573 | 1.583 |

The main improvement is avoiding off-screen submission. Caching bounds also accounts for much of the apparent advantage of a spatial index over recomputing bounds while chasing sprite/object pointers. Camera preparation alone produced no large, consistent improvement in this probe; its candidate uses public root-position getters, so this is not an isolated measurement of hoisting two divisions. The existing render path already reads synchronized world positions directly.

The rendering probe uses SDL 3.4.18's software renderer at 1280×720, GCC 16.2.1, `-O3 -DNDEBUG`, on an AMD Ryzen 5 7535HS. Each entry is a median of five batches in one process, with rotating execution order and warmup calls. These are exploratory measurements, not independent-process confidence estimates or GPU performance claims. Static candidates exclude bounds/index maintenance and use a fixed camera. Their small timing differences do not establish a general winner. The spatial probe isolates camera motion against a static scene and includes the existing SAP/grid camera maintenance, but excludes rendering and moving-sprite updates.

Correct bounds must follow the current rendering transformation. Sprite size includes object scale; the camera then scales width and height independently, and SDL rotates that destination rectangle about its center. This order is confirmed by the [SDL API documentation](https://wiki.libsdl.org/SDL3/SDL_RenderTextureRotated) and the vendored implementation. For output-space center `(cx, cy)`, scaled destination size `(W, H)` and sprite rotation `theta`, conservative half extents are:

```text
ex = (abs(cos(theta)) * abs(W) + abs(sin(theta)) * abs(H)) / 2
ey = (abs(sin(theta)) * abs(W) + abs(cos(theta)) * abs(H)) / 2
```

Reject only when this box is completely outside the viewport, with a small conservative rounding margin. The camera position is the view's top-left in the current code. Cached object sine/cosine values can be reused. Physics geometry is not the source of sprite bounds; renderer proxies must represent the actual destination quad. Source rectangles select texture coordinates, and flips preserve the centered quad's extent.

If storing equivalent bounds in world coordinates, nonuniform camera scaling changes the rotation contribution: `ex_world = (|c|*w + |s|*h*sy/sx)/2`, `ey_world = (|s|*w*sx/sy + |c|*h)/2` for positive sizes/scales. A cache/index must account for changes to this scale ratio. Camera translation alone need not rebuild world-space bounds. Using ordinary rotated world rectangles without considering the ratio can incorrectly cull visible pixels.

Pixel hashes matched the baseline for all ten rendering populations across all six render modes. Additional comparisons cover 200 sprites at the four viewport edges, camera translation, three camera aspect ratios, object scale, rotation, source rectangles, alpha blending and both flip axes. The spatial probe compared complete sorted result sets at sample positions and records matching aggregate hit counts across all timed methods. These checks do not cover every floating-point extreme, custom renderer viewport/logical-scale configuration, hierarchy edit, lifecycle transition or moving-sprite workload.

Implementation should preserve the existing sprite-vector order. For an index query, deduplicate sprites spanning cells and order results by the current `windowIndex`; sorting by texture would change overlapping transparent rendering. Bounds invalidation must cover synchronized parent/child translation, sprite dimensions (including animation changes), scale, rotation, registration/removal, scene replacement and relevant camera/output changes. Update render state after the runtime's mutation/transform commit, before drawing. For unsupported/nonfinite bounds, conservative submission is preferable to introducing disappearance through an uncertain rejection.

Before selecting an index for production, measure complete costs with 0%, 1%, 10% and 100% moving sprites, smooth pan, jumps, zoom/resize, large sprites and lifecycle churn. The bundled [Eigen KdBVH](../vendored/eigen-5.0.1/unsupported/Eigen/src/BVH/KdBVH.h) provides construction and traversal rather than a dynamic-tree update API; frequent rebuilding was not benchmarked. A lightweight grid or a static-index/moving-list split is worth comparing for that workload. A specialized camera-only SAP remains a valid candidate, but the data above supports neither a blanket preference for SAP nor a universal BVH recommendation.

The production engine library was rebuilt successfully. No engine regression suite was rerun because runtime code is unchanged; the investigation's checks are the two standalone probes. To reproduce on this host, run from the repository root (adjust the compiler-specific TBB directory on other hosts):

```bash
cmake --build bin/production --target platformator_engine --parallel 4
for name in probe spatial_probe; do
  c++ -std=c++20 -O3 -DNDEBUG \
    -Iinclude -Isrc -Ibin/production/generated \
    -Ivendored/SDL3-3.4.18/include -Ivendored/eigen-5.0.1 \
    -Ivendored/json -Ivendored/oneTBB-2022.3.0/include \
    "docs/evidence/p03/$name.cpp" \
    -Lbin/production -Lbin/production/gnu_16.2_cxx20_64_release \
    -Wl,-rpath,"$PWD/bin/production" \
    -Wl,-rpath,"$PWD/bin/production/gnu_16.2_cxx20_64_release" \
    -lplatformator -lSDL3_mixer -lSDL3_image -lSDL3_ttf -lSDL3 -ltbb \
    -o "/tmp/platformator-p03-$name"
done
/tmp/platformator-p03-probe > /tmp/p03-render.csv 2> /tmp/p03-render-verification.txt
/tmp/platformator-p03-spatial_probe > /tmp/p03-spatial.csv 2> /tmp/p03-spatial-verification.txt
```
