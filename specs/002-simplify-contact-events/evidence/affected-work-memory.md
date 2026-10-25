# Affected work and memory

Final work/contact/support CTest checks pass (3/3). The expanded churn cardinality/storage test also passes after respecting cleanup publication before confirming new geometry. Exact observed idle values: C++ new/new[] allocations 0; geometry visits 0; transition comparisons 0; event records 0; solver adjacency refresh visits 0. Script startup/replacement does not visit physical neighbors; sleeping-body changes do.

Five fixed-peak churn cycles each add sixteen confirmed contacts and return to original membership; event slots remain 256 and contact slots remain 512 after their first high water. Existing internal vector accessors provide this evidence without a public API addition. See final-work-values.txt.

Compiled baseline ColliderPair declaration: 152 bytes. Candidate: 136 bytes, exactly 16 bytes removed per candidate pair. This measures object layout, not allocator or whole-process memory. See pair-layout.json and measure_pair_layout.py.

Baseline observed-fixture peak child RSS: 14164 KiB. Candidate per-child samples: [14224, 13900, 14312, 14436, 14144] KiB; median 14224 KiB. The baseline lies within the candidate range; these coarse process measurements do not establish a net RSS improvement/regression. Candidate measurement uses wait4 per child to exclude inherited compiler/shell high-water marks; an earlier cumulative wrapper result was discarded. No claim is made about uninstrumented C malloc activity or every internal container.
