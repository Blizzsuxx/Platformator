# Contract: Filesystem Changes and Resource Discovery

**Requirements**: FR-007–014, FR-015, FR-021, FR-050. Initial limits and scheduling are [research decisions](../research.md), not measured tuning claims.

## Event boundary

`ProjectChangeService` accepts project/asset roots and one project generation; it emits normalized `ChangeBatch` values. A record identifies root, relative path, kind (`Added`, `Changed`, `Removed`, `RenameHint`, `RootDirty`), optional old path/cookie and backend sequence. A batch has generation, sequence range, affected paths/parents and completeness/status. Each directory has a monotonic invalidation revision; every listing request/result carries that revision plus request ID. Apply only the newest request whose revision still matches, otherwise discard/requeue it. A late older scan cannot replace a newer listing under the same project generation. Treat paths as hints and re-resolve them before acting. Paths are stored/displayed as UTF-8 with native conversion at OS boundaries; case semantics follow the filesystem and existing asset reference policy.

No native callback invokes ImGui, document commands, Runtime APIs or SDL texture functions. A serialized I/O dispatcher waits/reads native events and enqueues bounded records; CPU preparation uses oneTBB. The owner thread checks generations, resolves current logical/canonical paths, applies sorted bounded invalidations, and schedules needed work. Native queue overflow or coalescing uncertainty becomes RootDirty, never invisible loss.

## Startup, new directories, and root replacement

1. Assign a new project generation and establish root/parent monitoring before exposing an initial snapshot.
2. Discover directories with cancellation and bounds; install each required directory watch before enumerating its contents. For created/moved-in populated directories, repeat this handshake recursively.
3. Buffer events while discovery runs. Reconcile affected directories/paths and replay buffered events against the snapshot.
4. Mark coverage Current only when its snapshot and replay agree; otherwise remain Reconciling/Degraded and continue bounded recovery. Browser listings receive this baseline revision too, so a previously completed listing cannot hide a file already in the baseline.
5. Watch root identity/parent changes. On deletion/unmount/replacement, retire obsolete watches and alias resolutions, expose RootUnavailable/Degraded, and re-establish coverage when reachable.

Linux uses directory inotify watches, Windows subtree notifications plus root-parent coverage, and macOS FSEvents with explicit root/drop/rescan handling. Rename cookies are hints: unmatched halves, cross-root moves, or changed names still invalidate old/new path knowledge and affected parents. The editor does not infer a serialized-reference migration from a rename.

## Coalescing, incomplete writes, and fallback

Use a 200 ms quiet interval with a 1 second maximum postponement. A write hint increments path content version even if size/mtime are unchanged. Use write-completion signals where available; otherwise confirm two stable observations before adopting prepared content. Metadata stability is only a scheduling hint, not proof that a writer is finished. Fully validate/decode a candidate and swap it only if its content version and relevant identity still match at owner-thread commit. A changed-again version supersedes any in-flight load; later writes can supersede an already-valid intermediate version. Recommend atomic file replacement to asset-tool integrations. Retain last-good preview during incomplete writes; retry with bounded backoff for up to 30 seconds, then display Failed/Retry until a new event/manual request. A successful stale job cannot clear a newer failure/pending state.

A bounded consistency sweep runs at most once per 60 seconds in healthy idle monitoring. Under unsupported/degraded notifications, poll visible directories and referenced assets at two-second intervals, with source freshness checks before Build/Run. A root-wide scan reaching 50,000 files/100,000 entries is incomplete: retain previous complete data and coverage warnings, while targeted visible-folder/manual scans continue. Do not repeatedly restart the same incomplete whole-root scan inside a frame or raise bounds to hide the condition.

The event queue admits at most 8,192 unique paths/2 MiB; monitoring admits at most 4,096 directory watches. Exhaustion coalesces uncertainty to a root-rescan flag and publishes degraded status. Loss/overflow also advances an uncertainty epoch for the affected subtree, including source identity: metadata-identical entries cannot retain a supposedly current content version. Invalidate known consumers lazily by epoch, force visible/referenced content preparation first, and resolve the rest through bounded discovery. A metadata-identical recovery scan alone cannot clear that uncertainty. One recursive recovery scan per root and two pending targeted scans are allowed. Each targeted listing retains existing bounds of 10,000 files, 2,000 directories and 20,000 entries. Fallback reference checking uses at most 512 path-stat units per two-second interval in a visible-first round-robin worklist; expose backlog/incomplete coverage and never schedule one unbounded task per reference. Per-frame resource invalidation stops after 256 paths or 2 ms and resumes on the next scheduled wake. Every deferred batch retains age/coverage state so a backlog cannot masquerade as Current.

Manual Refresh rechecks the selected folder and referenced resources immediately through the same bounded scheduling path; it is available even when global monitoring is incomplete. A full project refresh is a distinct explicit action with coverage reporting.

## Containment, aliases, and changes to dependencies

Logical references remain case-preserving `assets/...`. Canonical targets must remain inside the canonical asset root. Maintain logical→canonical aliases and target→logical reverse aliases; an event at a contained target invalidates every alias consumer. Re-resolve on symlink/parent replacement and before use; reject escaping targets and traversal cycles. Broken links remain discoverable as unresolved entries. Parent watches/reconciliation cover link retargeting and target disappearance.

Changed parent listings preserve expanded/selected directory and selected entry if they still exist. Deleted targets clear only invalid selection and expose unresolved references; rename repair is a user command. An unrelated new image cannot clear all thumbnails, release Scene proxies, or change scene history. The dependency graph includes animation clip→frame textures→proxies, direct sprite textures, Inspector reference validity, diagnostics and source/catalog consumers.

For source/CMake paths, increment an event revision even if the old metadata fingerprint is unchanged. Effective source identity combines that revision and reconciled configured inputs. Exclude generated/build/vendor paths using existing project rules. Build/Run takes a current immutable identity and refuses stale launch after intervening changes. A notified same-metadata source write or uncertain source recovery also marks the next explicit build as requiring a one-time forced target rebuild, because a timestamp-driven build tool may otherwise reuse stale objects. Do not change source timestamps or automatically build on every watcher event; disclose the clean/rebuild reason and clear the flag only after a successful fresh build. Avoid periodic whole-source scans in healthy idle state beyond bounded consistency/freshness checks.

## Failure and lifecycle

Permission errors/remote filesystems/backend unavailability show explicit coverage limitations with copyable details. Notification delivery is advisory; scans confirm state, and unchanged metadata is not a content checksum. A successful prefix is not a complete snapshot.

On project switch, advance generation, stop/join native readers, cancel/join preparation and discard queued old records before adopting new roots. Tab closure invalidates its consumers without stopping project-wide monitoring. No event completion may use a stale renderer/document pointer. Root recovery creates new watch/content generations rather than reusing obsolete identities.

## Acceptance boundary

Deterministic injected tests exercise startup overlap, add/change/delete/paired and unpaired rename, populated moved-in directories, same-size/restored-timestamp writes, partial writes, root replacement, event overflow, watch exhaustion, scan truncation, permissions, aliases/broken/cyclic/escaping links and stale project results. Small real filesystem tests verify a file added to an already-loaded folder appears within three seconds after settle on supported local filesystems. Assert zero unrelated resource reloads/history mutations, selection preservation, visible degradation, manual/visible-folder fallback, and cancellation/teardown safety. These are feature checks, not a large benchmark campaign.
