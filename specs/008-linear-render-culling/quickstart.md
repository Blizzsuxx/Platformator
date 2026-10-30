# Quickstart: Linear Rendering Culling Validation

> **Historical workflow:** Do not restart this exhaustive campaign or recreate its deleted artifacts. Use the [compact rendering quickstart](../009-incremental-render-culling/quickstart.md) and [benchmark policy](../../AGENTS.md). Legacy campaigns now require explicit opt-in and larger sample counts must be specified deliberately.


The T044 evidence is retained under `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`, which is ignored by Git. It contains the pinned baseline and candidate sources/builds, checked rendering/resource proofs, all timing samples, logs, provenance, and a checksum-backed relocatable index. See [capture-index.md](evidence/capture-index.md) and the [validation contract](contracts/validation-evidence.md).

## Verify the retained evidence

From the bundle root:

```sh
scripts/verify_bundle.sh
```

This checks the bundle file checksums, rehashes each source input and embedded binary identity, revalidates all 51 rendering/resource proofs and all 315 timing/control captures, refreshes the bundle-relative report/index view, and checks the file inventory again. It does not run builds or timings. To relocate the bundle, move it as a unit and run `python3 scripts/relocate_index.py`; captured report/raw-index files remain unchanged and the normalized views use paths relative to the new root.

The actual run used SDL software rendering with 320×240 output, scale 1×1, default viewport, no active target/logical presentation, and VSync off. It recorded CPUs 0–11 and worker counts 1 and 6. Stock-runner manifests omit renderer identity; see [RESULTS.md](evidence/RESULTS.md).

## Reproduce into a new persistent run directory

The run root must be fresh and Git-ignored before capture. The repository ignore rule covers `benchmark/artifacts/linear-render-culling/`. The self-contained script copies the retained source archives and scripts, then reconstructs the baseline, builds both configurations, runs correctness/resource validation, completes those checks before timing, and writes the new run bundle:

```sh
git check-ignore -v benchmark/artifacts/linear-render-culling/<new-run-id>
benchmark/artifacts/linear-render-culling/20261029-t044-run-01/scripts/reproduce.sh \
  benchmark/artifacts/linear-render-culling/<new-run-id>
```

Choose a new run ID; the script refuses to overwrite an existing destination. Source/build inputs and outputs stay below that persistent directory. No `/tmp` file is required.

## Build and correctness checks

The candidate geometry oracle uses independent double-precision transformed corners. Image/order and resource observations require separate Release builds with work probes enabled; timing builds must have debug tools, work probes, and sanitizers disabled. The T044 bundle contains the actual build commands/logs and completed CTest logs: baseline 29/29 (package-smoke excluded) and candidate 34/34 passed.

## Focused capture interfaces

`platformator_linear_render_probe` accepts `visible`, `offscreen`, `sparse`, `clustered`, `crossing`, `camera-pan`, `churn`, `rotated`, and `--headless`. Common options include `--sprite-count`, `--workers`, `--seed`, `--warmup-frames`, `--measure-frames`, `--dt`, and `--csv-output`.

- `--validate-rendering --evidence-output PATH` writes per-frame image/order proof data in a work-probe build.
- `--validate-resources --evidence-output PATH` writes separate allocation/work/RSS observations.
- `--validation-proof PATH --resource-proof PATH` links a timing run to the previously checked, matching proofs.
- `--no-render-culling` selects candidate submit-all mode. The pinned pre-feature build is the third reference.

Rendering/resource validation is marked `validation_mode=per-step`, `timing_eligible=false`, and never enters timing. C++ allocation interception does not count aligned `new`, C allocation, SDL, or graphics-driver allocations. RSS is process-wide.

## Bounded timing driver

The T044 script records the exact bounded invocation, pins the driver and children to CPUs 0–11, and writes the 255 focused plus 60 stock-control processes below the run root. The underlying tool requires separate baseline/candidate timing probes, validation probes, stock runners, proof roots, a shared asset root, five repetitions, and a fresh output path. Timing starts only after all builds, CTests, rendering proofs, and resource observations complete. See `benchmark/README.md` and `scripts/run_timing.sh` in the bundle.

## Current evidence status

Sparse complete-render means improved by 73.9–85.0% across the bounded sparse cases, with independent-run intervals below zero. All-visible mean estimates were positive but their 95% intervals included zero; they are reported as potential overhead with uncertain magnitude. Headless rendering work stayed at zero. See [RESULTS.md](evidence/RESULTS.md) for per-case means, tails, stock controls, resource observations, and limitations.
