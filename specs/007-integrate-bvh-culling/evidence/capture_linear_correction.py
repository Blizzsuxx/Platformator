#!/usr/bin/env python3
"""Generate and capture bounded matched evidence for the ordered linear visibility fix."""

import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "benchmark"))
from generate_spatial_traces import engine_case  # noqa: E402


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = Path(__file__).resolve().parent
OUTPUT = EVIDENCE / "convergence" / "linear-correction"
GENERATOR = ROOT / "benchmark" / "generate_spatial_traces.py"
SWEEP = ROOT / "benchmark" / "sweep_spatial.py"

REPLAY_CASES = {
    "visibility-n1000-p0",
    "visibility-n1000-p100",
    "visibility-n1000-change10-moving",
    "visibility-n1000-change10-pan",
}
ENGINE_CASES = {"engine-sprite-visible0", "engine-sprite-visible100"}


def generate_subset(preset, selected, target, warmup, measure):
    target.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="platformator-linear-case-") as temporary:
        generated = Path(temporary) / "generated"
        subprocess.run([sys.executable, str(GENERATOR), "--preset", preset,
                        "--seed", "0", "--warmup-frames", str(warmup),
                        "--measure-frames", str(measure), "--output-dir", str(generated)],
                       cwd=ROOT, check=True)
        manifest = json.loads((generated / "cases.json").read_text())
        cases = [case for case in manifest["cases"] if case["case_id"] in selected]
        if {case["case_id"] for case in cases} != selected:
            raise RuntimeError(f"Generator did not produce the requested {preset} cases")
        if preset == "visibility":
            for case in cases:
                shutil.copy2(generated / case["trace"], target / case["trace"])
        manifest["cases"] = cases
        (target / "cases.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")


def run_bundle(name, cases, runner, resource_runner, environment):
    base = OUTPUT / name
    validation = base / "validation"
    timing = base / "timing"
    resources = base / "resources"
    common = [sys.executable, str(SWEEP), "--runner", str(runner),
              "--cases", str(cases), "--broad-phases", "segmented",
              "--render-cullings", "none,linear,bvh,segmented",
              "--worker-counts", "1,6", "--available-workers", "6",
              "--sweep-seed", "42"]
    commands = [
        ("validate", common + ["--repetitions", "1", "--capture-mode", "validate",
                                "--output-dir", str(validation)]),
        ("timing", common + ["--repetitions", "5", "--capture-mode", "performance",
                              "--validation-dir", str(validation), "--output-dir", str(timing)]),
        ("resources", [sys.executable, str(SWEEP), "--runner", str(resource_runner),
                       "--cases", str(cases), "--broad-phases", "segmented",
                       "--render-cullings", "none,linear,bvh,segmented",
                       "--worker-counts", "1,6", "--available-workers", "6",
                       "--sweep-seed", "42", "--repetitions", "1", "--capture-mode", "resource",
                       "--timing-dir", str(timing), "--output-dir", str(resources)]),
    ]
    for label, command in commands:
        log_path = base / f"{label}.log"
        print(f"Starting {name} {label}", flush=True)
        with log_path.open("w") as log:
            result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                                    stderr=subprocess.STDOUT)
        if result.returncode:
            raise subprocess.CalledProcessError(result.returncode, command)
        print(f"Finished {name} {label}", flush=True)


def run_dynamic_engine_bundle():
    bundle = OUTPUT / "engine-dynamic"
    cases_directory = bundle / "cases"
    cases_directory.mkdir(parents=True, exist_ok=False)
    generator_source = hashlib.sha256(GENERATOR.read_bytes()).hexdigest()
    typed_cases = [
        engine_case(f"engine-sprite-visible{visible}-moving", "sprite_visibility",
                    {"sprite_count": 1000, "visible_percent": float(visible),
                     "changed_percent": 10.0, "variant": "moving"}, True,
                    31 + visible, 120, 600, 1.0 / 120.0)
        for visible in (1, 100)
    ]
    cases_path = cases_directory / "cases.json"
    cases_path.write_text(json.dumps({
        "format": "platformator-spatial-cases", "version": 1,
        "generator": {"name": "generate_spatial_traces.py", "revision": "spatial-trace-generator-v2",
                      "source_hash": generator_source},
        "cases": typed_cases,
    }, sort_keys=True, indent=2) + "\n")
    (bundle / "case-selection.json").write_text(json.dumps({
        "purpose": "T095 invalidated actual-render frames at sparse and full initial visibility",
        "case_ids": [case["case_id"] for case in typed_cases],
        "changed_percent": 10,
        "workers": [1, 6],
        "methods": ["none", "linear", "bvh", "segmented"],
        "timing_processes_per_configuration": 5,
        "sweep_seed": 42,
    }, indent=2) + "\n")
    environment = dict(os.environ, SDL_VIDEODRIVER="offscreen",
                       SDL_RENDER_DRIVER="software", SDL_AUDIODRIVER="dummy")
    runner = ROOT / "bin/cleanup-benchmark/platformator_benchmark_runner"
    resource_runner = ROOT / "bin/cleanup-benchmark/platformator_spatial_resource_runner"
    run_bundle("engine-dynamic", cases_path, runner, resource_runner, environment)


def main():
    import argparse

    global OUTPUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dynamic-only", action="store_true",
                        help="capture invalidated live visibility cases into an existing bundle")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT,
                        help="fresh bundle directory (defaults to evidence/convergence/linear-correction)")
    args = parser.parse_args()
    OUTPUT = args.output_dir.resolve()
    if args.dynamic_only:
        run_dynamic_engine_bundle()
        return
    if OUTPUT.exists() and any(OUTPUT.iterdir()):
        raise FileExistsError(f"Refusing to overwrite existing evidence: {OUTPUT}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    replay_cases = OUTPUT / "replay" / "cases"
    engine_cases = OUTPUT / "engine" / "cases"
    generate_subset("visibility", REPLAY_CASES, replay_cases, 10, 30)
    generate_subset("engine", ENGINE_CASES, engine_cases, 120, 600)
    (OUTPUT / "case-selection.json").write_text(json.dumps({
        "purpose": "T095 ordered linear visibility correction",
        "replay_case_ids": sorted(REPLAY_CASES),
        "engine_case_ids": sorted(ENGINE_CASES),
        "workers": [1, 6],
        "methods": ["none", "linear", "bvh", "segmented"],
        "timing_processes_per_configuration": 5,
        "sweep_seed": 42,
    }, indent=2) + "\n")
    environment = dict(os.environ, SDL_VIDEODRIVER="offscreen",
                       SDL_RENDER_DRIVER="software", SDL_AUDIODRIVER="dummy")
    runner = ROOT / "bin/cleanup-benchmark/platformator_benchmark_runner"
    resource_runner = ROOT / "bin/cleanup-benchmark/platformator_spatial_resource_runner"
    run_bundle("replay", replay_cases / "cases.json", runner, resource_runner, environment)
    run_bundle("engine", engine_cases / "cases.json", runner, resource_runner, environment)


if __name__ == "__main__":
    main()
