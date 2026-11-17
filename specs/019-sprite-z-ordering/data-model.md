# Data Model: Sprite Z Ordering and Render Culling

## Authored Sprite value

| Field | Type / default | Owner and rules |
|---|---|---|
| `zOrder` | `std::int32_t`, zero | Sprite; authored, cloned and serialized. Lower values draw earlier. |

All constructors use the same default member initialization. `setZOrder` returns on equal values. On a real edit, assign the value and notify the window only when registered. The window's optional state makes renderer-free operation a no-op for tracking. There is no z-dependent geometry or parent inheritance.

JSON uses optional `zOrder`. Missing means zero, even when reading into an existing Sprite. A present value must be an integer in [-2147483648, 2147483647]; reject booleans, null, float encodings, strings, containers, and out-of-range unsigned/signed integers before narrowing. Validation belongs at this boundary, not inside an already typed setter.

## Render culling storage

The window resolves its startup policy once. A transient ineligible view bypasses cached selection for that frame. A later eligible view checks current keys and pending geometry edits before publishing membership. The culling opt-out or current-linear selection on a renderer still requires Z ordering over the registered slots.

| Owner | Lifetime and contents |
|---|---|
| Both cached policies | One common record per registered slot stores sprite handle, certified render bounds, eligibility flags and inverse retained position. The dirty queue and its positions belong to the shared cache. |
| Indexed policy only | The existing spatial index owns a dense array of grid metadata by slot, containing up to four link IDs, their count, side-list position, spatial classification and last-query stamp. Its cells, links, pools, fallback list and query scratch exist only for indexed mode. |
| Direct current-linear, submit-all, renderer-free | No cached culling records. Only a live renderer owns draw-order storage. |

On indexed registration, reserve the common record and grid metadata together before publishing the new slot. On removal, unlink the retiring slot, fix the moved slot's grid links and side-list entry, and move the grid metadata with the common record. Preserve deduplication stamp wrap and inverse visible positions. Release the index arrays together with common state on successful retirement. Capacity observations include both sets of arrays and the selection scratch exactly once. The default's grid-specific byte count must be zero after registration, growth and ordinary churn.

## Renderer-owned ordering state

`SDLWindow` exclusively owns an optional private `SpriteDrawOrder` state, created only with a live renderer. It contains no external ownership and introduces no public registry.

| Field | Purpose and invariant |
|---|---|
| `sequenceBySlot` | `vector<uint64_t>` aligned exactly with `spriteComponents`. A sequence is unique in the current registration epoch and follows its sprite on swap removal. |
| `entries` | Reusable `vector<Entry>`, each entry holding `{size_t rank, int32_t zOrder, uint64_t registrationSequence}`. Valid only when not dirty and prepared membership matches. Rank refers to the dense sprite registry; keys are copied for comparison locality, not visual-content caching. |
| `nextSequence` | Next registration sequence. Assigned at successful registration commit, not from worker execution or asset IDs. Reset only after the old scene's state is retired. |
| `orderDirty` | Starts true. Set by a relevant Z Order edit or structural change. Preparation separately checks the effective set and its revision. One preparation consumes all preceding edits. An edit outside a known prepared subset does not set it. |
| `preparedAll` | Whether prepared candidates cover every current registered slot. This represents an actual candidate set, including submit-all fallback, not a renderer mode. |
| `preparedMembershipRevision` | Last consumed culling membership revision for a proper subset. It must match the current culling revision before that subset can answer an edit-membership check. Irrelevant when `preparedAll` is true. |

No order-change queue, per-sprite order-dirty bit, hash/map membership, global sorted population, or packed depth/texture key is added. The setter passes the registered slot to the window. The window checks prepared membership in O(1): a current all-sprite order contains the slot; a current subset uses the culling record's inverse position; an unprepared or stale set conservatively dirties ordering. An offscreen edit in a current subset leaves the prepared order reusable. If that sprite later enters the view, the membership change rebuilds entries from its latest value. Multiple visible edits before preparation coalesce in `orderDirty`.

Ordering vectors grow geometrically before registration commits and retain capacity during ordinary churn. Retained capacity is bounded by peak live population since the latest successful scene reset, rather than historical positions, edits or identities. Invariant violations use assertions where useful; live drawing does not repeatedly resolve handles or catch internal errors.

### Prepared-candidate selection

For N registered sprites, let `currentAll` be true when preparation falls back to all sprites or a valid culling result contains N unique ranks. The latter implies the complete set without an extra scan.

| Condition | Preparation action |
|---|---|
| No ordering state or renderer | No ordering preparation. |
| `orderDirty` | Rebuild from current candidates, sort entries once by `(zOrder, registrationSequence)`, consume final keys. |
| `currentAll != preparedAll` | Membership changed; rebuild. |
| Both are all, not dirty | Reuse; transitions between culling and fallback do not matter when the actual set remains all sprites. |
| Both are proper culled subsets and membership revision unchanged | Reuse. |
| Both are subsets but membership revision changed | Rebuild; equal counts alone do not establish equal sets. |

After rebuilding, publish `preparedAll` and the current subset revision, and clear `orderDirty`. Drawing uses current `spriteComponents[entry.rank]`, existing active/eligibility rules, and the current view's submission/culling behavior. A cached all-sprite set does not force cached-culling submission flags on a fallback frame.

## Culling membership revision

The existing culling records and `currentVisibleRanks` remain authoritative for membership, while `entries` is the only ordered draw list. `currentVisibleRanks` is a dense, unsorted vector. Each culling record has `visiblePosition`, the vector index for its rank, or an `absentPosition` sentinel. This replaces numeric `lower_bound` membership tests and rank-sorted merges.

Full reselection builds unique ranks in the existing reserved `visibleRankMerge` scratch. The indexed query's query stamps deduplicate candidates, and the linear path visits each rank once. Before publishing, compare counts and check that every new rank has a non-sentinel old `visiblePosition`. Because both sets are unique, equal counts plus every new rank belonging to the old set prove exact set equality even when enumeration order differs. If equal, clear scratch and leave the revision unchanged. Mark the selection valid at publication even when the first query returns an empty set. Otherwise, mark old positions absent, swap the vectors, assign each new inverse position, and advance `visibleMembershipRevision` once. Do not expose the scratch selection to drawing before publication.

Delta culling classifies only geometry-dirty ranks. An actual visibility insertion appends its rank and records its position. Removal swaps the last rank into the removed position, updates that rank's inverse position, pops the vector, and marks the removed record absent. These membership edits are O(1) each. Advance the revision once after a batch with any actual membership change. Same-membership camera/projection work may visit candidates and compare membership during visibility preparation, but it does not sort ranks or rebuild draw entries.

On dense-record removal at rank `i`, first remove `i` from the visible vector if present. Then, if the final record at rank `last` moves to `i` and is visible, patch its vector element from `last` to `i` through its inverse position before moving the record and invalidating the retired rank. New records start with the absent sentinel. The removal and any visible-rank patch are O(1), including when removing the last slot. Invalidate prepared draw entries before retirement so no cached rank can address the removed sprite. Registration and structural rank changes invalidate prepared ordering and advance or invalidate membership revision even when the removed sprite was offscreen.

An unchanged cached frame checks scalar validity only. It does not compare sprite order keys, scan for edits, or sort membership. A changed membership set is copied to draw entries and sorted once by the total draw key. The scratch vector is reused on the next full query.

### Membership and invalidation checks

- Full reselection with the same ranks in a different query order leaves the membership revision unchanged. Equal-sized sets with one rank replaced advance it. Candidate producers must provide unique, in-range ranks.
- Delta insertion, removal, no-op changes, and batches preserve both directions of the `currentVisibleRanks`/`visiblePosition` relation. Each actual batch advances the revision once.
- Dense removal covers visible/invisible removed ranks crossed with visible/invisible final ranks. The vector contains no retired rank or duplicate, and moved records resolve to the patched rank.
- A prepared proper subset skips a Z edit outside that set in O(1). A Z edit inside it dirties order. All-sprite preparation dirties on any real Z edit. Unprepared or revision-stale membership conservatively dirties order.
- After an offscreen edit, a later camera query that admits the sprite prepares its latest Z value. Fallback/all and subset transitions prepare exactly the current candidate set.
- Warm unchanged frames perform no order comparisons or reconstruction. A relevant membership change or visible edit, including one visible edit, may rebuild and sort all V entries once. Any incremental one-entry repositioning is a comparison prototype only and must use the same independent order oracle.

## Registration and retirement transitions

| Event | State transition |
|---|---|
| New live registration | Reserve registry/metadata/draw capacity before commit; append a fresh sequence; set the culling record's `visiblePosition` to `absentPosition`; dirty/clear prepared entries. |
| Real Z Order edit | Update authored value; use prepared-set membership for an O(1) invalidation check; dirty order only when the sprite is in the prepared set or membership is stale/unprepared. Leave geometry/bounds/index untouched. |
| Identical assignment | No change or notification. |
| Unregister at slot i | Invalidate entries before retirement; remove `i` from visible membership and patch a moved last rank through `visiblePosition`; move last sprite's sequence to `i` with the dense slot; pop sequence; preserve all survivor keys. |
| Offscreen/texture loss and restoration | No registration-sequence change. Visibility changes may change candidates; current value is used upon preparation. |
| Inactivation followed by activation | Existing GameManager reconciliation really unregisters/re-registers the Sprite; assign fresh precedence on the new registration. An inactive/active toggle coalesced before reconciliation follows whatever actual registration transitions occur. |
| Scene prepare fails | Keep live scene and all of its ordering state intact. |
| Successful scene clear/replacement | Existing commit retires old sprites, then `releaseRenderingStorage` releases ordering vectors and culling storage, resets sequence/prepared metadata for the next epoch. |
| Shutdown | Clear before object/runtime retirement; destroy the optional state with its window owner. |

No-camera frames keep pending dirty state until drawing is needed. Renderer lifetime is fixed by window creation/close; dynamic renderer attachment is not introduced.

## Editor document and preview

The document adds ordinary Sprite field `zOrder`, default zero. Existing Sprite change category, history, template field paths and component settings schema carry it; no new document category or field-specific template protocol is needed. Validation includes it in the existing component fingerprint so later edits recheck the affected descriptor.

Preview has an independent manual draw loop. Maintain:

- Per-proxy cached `zOrder`, separate from texture/animation inputs, and a stable registration sequence. A genuinely new/reactivated registration receives a fresh sequence; ordinary value edits, view changes and texture availability changes do not.
- One reusable `drawDocumentIds_` sequence plus a dirty bit and next-sequence counter. Rebuild it only on ordering/membership changes, using the same total key. Keep missing-texture and offscreen proxies in that stable list; existing rendering filters determine actual submission without changing keys.
- Existing `orderedDocumentIds_` remains document traversal order. Clear draw IDs before removing proxies and prepare a current list at document-batch/render/picking boundaries. Reset capacities/state with existing preview reset/close handling.

Apply ordering-only input changes before the expensive texture-input early return; call the Sprite setter and dirty the draw list/image without refreshing assets or geometry. The cached visual sequence is also available to the viewport: replace only the sprite-bearing subsequence of existing pickable IDs with visible-order IDs filtered by the same hidden/locked rules. Leave non-sprite slots and within-object target priorities intact, then use the existing reverse traversal/cycling. This is a small consumer of cached IDs, not another sort or persistent pick index.

## Observation-only data

Extend existing work-probe counters for `spriteOrderPreparations`, `spriteOrderEntries`, `spriteOrderComparisons`, `visibleMembershipChecks`, and `visibleMembershipChanges`; reuse allocation/retirement observers. Counters are reset/read through existing test facilities and compiled out of timing builds. In-memory correctness observations may use identities/pixels, but default retained benchmark artifacts contain only bounded summaries, provenance and raw timing samples.

The culling selection retains one rank vector and one same-capacity reusable full-reselection scratch vector. Add one `RecordIndex visiblePosition` per culling record. Reuse the old reserved merge vector as reselection scratch; remove the visible-rank change vector and numeric merge/sort work. Ordering reserves its sequence and draw-entry vectors separately. Measure actual capacities and record sizes; warmed membership updates and ordering preparation must not allocate when capacity is sufficient.

Work bounds are O(1) for each delta membership insertion/removal and for an edit's prepared-set membership check; O(V) for exact full-reselection set comparison; O(V log V) for one draw-entry sort after a changed membership set or relevant visible Z edit; and O(1) ordering validity checks on unchanged frames. Full query visibility classification costs remain part of the existing culling path. With clean retained keys, an equal full-query set reuses the draw entries. Publishing a changed full-query set visits O(previous V + new V) positions.
