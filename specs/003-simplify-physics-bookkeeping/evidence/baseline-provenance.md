# Baseline provenance

Implementation starts at f6d1af73fcb9a9e623b735fe0a8269d6e65c8fba on actual branch 001-cpp-engine-cleanup. No runtime drift from planned f6d1af7; initial changes are staged feature-003 documents only. The initial staged and unstaged patches are retained alongside this file. Historical 7906959 is context only; current schema-3 runtime is the control.

Capture root: `/home/user/Projects/Platformator/bin/physics-bookkeeping-20261025`. Isolated complete source/build tree: `/home/user/Projects/Platformator/bin/physics-bookkeeping-20261025/baseline-source`. Runner/build paths are recorded after build. Neither existing raw captures nor staged files are rewritten.

See initial-environment.json for compiler, CMake, Python, CPU, governor and allowed affinity. Selected workers 1/6, masks CPU0 / CPUs0,2,4,6,8,10; these are within the allowed set. Dependencies are the tracked vendored inputs. Existing ignore rules cover bin/build/dist, C++ binaries, Python caches, logs and environment files; no additional ignore file is needed for this C++/Python scope.

No extension hooks are registered. Requirements checklist has 16/16 checked markers and is read-only; its initial checksum is retained. Runtime edits are barred until baseline and characterization gates complete.

Isolated cleanup-debug, cleanup-work and cleanup-benchmark builds completed successfully. Build logs: /home/user/Projects/Platformator/bin/physics-bookkeeping-20261025/logs. Each preset keeps its own compiled vendored libraries under baseline-source/bin/. Release runner: /home/user/Projects/Platformator/bin/physics-bookkeeping-20261025/baseline-source/bin/cleanup-benchmark/platformator_benchmark_runner. No baseline runtime code was modified.

Final Release source fingerprints verified against current actual file contents and generated metadata: baseline5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031; candidate6d095977a927bd8170521f61894abd01eabd8dc2349d3b376926771e96de4c41. No runtime edits planned during final captures. ldd confirms separate vendored library paths (isolated-runtime-libraries.txt). Common characterization-final.patch records test-only inputs.
