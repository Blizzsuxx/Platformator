> Historical pre-convergence record. Current implementation, qualification and completion evidence are in [convergence/IMPLEMENTATION.md](convergence/IMPLEMENTATION.md), [final-validation.md](final-validation.md) and [RESULTS.md](RESULTS.md). Earlier raw data/source identities remain unchanged.

# Selected-backend work validation (T030)

The probes-enabled `platformator_runtime_work_test bvh 1` and `bvh 6` CTest cases passed. Both
created 2,048 colliders and confirmed one exact candidate pair. The runtime owned 2,048 BVH proxies
and stable records with no active Grid. Initial publication added one proxy per collider and ran
one initial maintenance pass; the six-worker parallel query policy observed query concurrency 6.
The one-worker run observed concurrency 1.

After warming the confirmed contact, 60 unchanged frames had zero BVH adds, removes, moves,
maintenance passes, queried endpoints, reconciled pairs, allocations or inactive-Grid operations.
A contained resize queried only its dirty endpoint and rechecked retained adjacency, while doing
no tree move or maintenance pass; exact candidate membership was removed when exact bounds no
longer overlapped. Retirement reconciled state before reclaiming its record, and a replacement
reused the stable slot without increasing record capacity.

The headless integration matrix separately checked an unchanged persistent contact over 16 idle
frames: one pair remained registered and the two initial Enter callbacks were not repeated. The
existing complete physics/lifecycle tests remain part of the US1 acceptance run.

Captured probe outputs:

| Run | Output | SHA-256 |
|---|---|---|
| One worker | `bvh proxies=2048 record_capacity=2048 idle_allocations=0 query_concurrency=1` | `f809e4530ff8a6fe049fd21172171403f8d5b8cd8a027b777b67c1401370c398` |
| Six workers | `bvh proxies=2048 record_capacity=2048 idle_allocations=0 query_concurrency=6` | `0043cb94a5f1996e4e79778fd5c0036c3c42d59e9f25e125eb8dad37bba4dc50` |

These are affected-work observations from a probes build, not timing measurements.
