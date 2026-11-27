"""Capture one paired Box2D v3.2.0 and Platformator physics workload."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys

from capture_budget import CaptureBudget


ENGINES = ("Box2D v3.2.0", "Platformator")
SCENES = {"large_pyramid": (5050, 1), "many_pyramids": (22000, 20), "smash": (9601, 0)}
BOX2D_COMMIT = "178495ba35695aefe9c321e569d2277f06db3b90"


def capture_run(budget, command, output, environment):
    budget.check()
    diagnostics = output.with_suffix(".stderr.log")
    with output.open("xb") as stdout, diagnostics.open("xb") as stderr:
        with subprocess.Popen(command, stdout=stdout, stderr=stderr, env=environment) as process:
            try:
                returncode = budget.wait(process)
            finally:
                if process.poll() is None:
                    process.kill()
    if returncode:
        raise RuntimeError(f"Exit {returncode} from {shlex.join(command)}. See {diagnostics}")
    if diagnostics.stat().st_size == 0:
        diagnostics.unlink()
    return json.loads(output.read_text(encoding="utf-8"))


def validate_run(run, args, engine):
    expected = {"scenario": args.scenario, "workers": args.workers, "dt_seconds": 1 / 60,
                "warmup_steps": args.warmup_steps, "measure_steps": args.measure_steps}
    if any(run[key] != value for key, value in expected.items()):
        raise RuntimeError(f"Unexpected run settings from {engine}")
    identity = run["identity"]
    if identity["engine"] != engine or identity["configuration"] != "Release":
        raise RuntimeError(f"Wrong engine or build configuration for {engine}")
    if engine == "Box2D v3.2.0" and identity["commit"] != BOX2D_COMMIT:
        raise RuntimeError("Expected the pinned Box2D v3.2.0 checkout")
    if engine == "Platformator" and (identity["sanitizer"] != "none" or identity["work_probes"]):
        raise RuntimeError("Build Platformator without sanitizers and work probes for timing")

    dynamic, static_shapes = SCENES[args.scenario]
    static_bodies = min(static_shapes, 1) if engine == "Box2D v3.2.0" else static_shapes
    for boundary in ("initial", "before_measurement", "after_measurement"):
        state = run[boundary]
        awake = state["awake_dynamic_bodies"]
        if (state["bodies"] != dynamic + static_bodies or state["shapes"] != dynamic + static_shapes
                or not 0 <= awake <= dynamic):
            raise RuntimeError(f"Unexpected {engine} population at {boundary}")
        if args.scenario != "smash" and awake != dynamic:
            raise RuntimeError(f"Sleeping bodies in {engine} pyramid at {boundary}")
    if args.scenario == "smash" and run["initial"]["awake_dynamic_bodies"] != 1:
        raise RuntimeError(f"Expected only the projectile to start awake in {engine} smash")
    samples = run["samples_ns"]
    if len(samples) != args.measure_steps or any(type(sample) is not int or sample <= 0 for sample in samples):
        raise RuntimeError(f"Invalid timing samples from {engine}")


def capture(args):
    runners = dict(zip(ENGINES, (args.box2d.resolve(strict=True), args.platformator.resolve(strict=True))))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    budget = CaptureBudget([output])
    settings = ["--scenario", args.scenario, "--workers", str(args.workers),
                "--warmup-steps", str(args.warmup_steps), "--measure-steps", str(args.measure_steps)]
    sdl = {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "SDL_RENDER_DRIVER": "software"}
    commands = {engine: [str(path), *settings] for engine, path in runners.items()}
    report = {"scenario": args.scenario, "workers": args.workers, "warmup_steps": args.warmup_steps,
              "measure_steps": args.measure_steps, "repetitions": args.repetitions,
              "capture_command": sys.argv, "commands": commands, "sdl_environment": sdl,
              "system": platform.platform(), "cpu_affinity": sorted(os.sched_getaffinity(0))
              if hasattr(os, "sched_getaffinity") else None, "engines": {}}
    cpu_info = Path("/proc/cpuinfo")
    if cpu_info.is_file():
        report["cpu_model"] = next((line.partition(":")[2].strip() for line in cpu_info.read_text().splitlines()
                                    if line.startswith("model name")), None)
    for engine, path in runners.items():
        with path.open("rb") as executable:
            digest = hashlib.file_digest(executable, "sha256").hexdigest()
        report["engines"][engine] = {"executable_sha256": digest, "runs": []}

    # Alternate process order between repetitions on the same inherited CPU affinity.
    for repetition in range(1, args.repetitions + 1):
        for engine in ENGINES if repetition % 2 else reversed(ENGINES):
            label = "box2d" if engine == ENGINES[0] else "platformator"
            raw = output / f"{label}-{repetition}.json"
            run = capture_run(budget, commands[engine], raw, {**os.environ, **sdl})
            validate_run(run, args, engine)
            engine_report = report["engines"][engine]
            if engine_report.setdefault("identity", run["identity"]) != run["identity"]:
                raise RuntimeError(f"Build identity changed during the {engine} comparison")
            engine_report["runs"].append({"file": raw.name,
                                          "median_ms": statistics.median(run["samples_ns"]) / 1e6})
    for engine_report in report["engines"].values():
        engine_report["median_of_run_medians_ms"] = statistics.median(run["median_ms"] for run in engine_report["runs"])
    contents = json.dumps(report, indent=2) + "\n"
    budget.check(reserve_bytes=len(contents.encode()))
    (output / "report.json").write_text(contents, encoding="utf-8")
    for engine, result in report["engines"].items():
        print(f"{engine}: {result['median_of_run_medians_ms']:.3f} ms/step (median of run medians)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platformator", type=Path, required=True)
    parser.add_argument("--box2d", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", choices=SCENES, default="large_pyramid")
    parser.add_argument("--workers", type=int, metavar="1–32", default=1)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--warmup-steps", type=int, default=5)
    parser.add_argument("--measure-steps", type=int, default=30)
    args = parser.parse_args()
    if not 1 <= args.workers <= 32:
        parser.error("Provide 1–32 workers")
    if args.repetitions < 1 or not 0 <= args.warmup_steps <= 100000 or not 1 <= args.measure_steps <= 100000:
        parser.error("Provide positive repetitions and measured steps, and 0–100000 warmup steps")
    try:
        capture(args)
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        parser.exit(1, f"Capture stopped. {error}\n")


if __name__ == "__main__":
    main()
