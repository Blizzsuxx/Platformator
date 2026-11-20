import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics as stats
import subprocess
import sys

from diagnose import collect_diagnostics
from prepare import build_audit_runner, root, tools
sys.path.insert(0, str(root / "engine/benchmarks"))
from capture_budget import CaptureBudget


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

cases = {
    "trigger_dense": ("narrow_phase", ["--narrow-count", "200"], {}),
    "single_container": ("rigid_body_container", ["--box-count", "500", "--circle-count", "500"], {}),
    "eight_containers": ("rigid_body_container", ["--box-count", "63", "--circle-count", "62"],
                         {"PLATFORMATOR_AUDIT_PILES": "1"}),
    "trigger_validation": ("narrow_phase", ["--narrow-count", "20"], {}),
}
config = {
    "serial": {},
    "short": {"PLATFORMATOR_AUDIT_SHORT_COMMIT": "1"},
    "graph": {"PLATFORMATOR_AUDIT_GRAPH": "1"},
}

def save():
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    budget.check()


def read_csv(path):
    with path.open() as file:
        return list(csv.DictReader(file))


def run(case, variant, trial, *, validation=False, repetitions=1):
    workers = 1 if validation else 6
    warmup = 120
    steps = 240 if validation else 600
    scenario, extra_arguments, extra_env = cases[case]
    mode = "validation" if validation else "capture"
    prefix = output / f"{case}-{variant}-w{workers}-{mode}-{trial}"
    csv_path = prefix.with_suffix(".csv")
    manifest = prefix.with_suffix(".manifest.json")
    probe = prefix.with_suffix(".profile.csv")
    raw = prefix.with_suffix(".samples.csv")
    logfile = prefix.with_suffix(".log")
    env = {**config[variant], **extra_env, "PLATFORMATOR_AUDIT_SAMPLES": str(probe)}
    command = ["taskset", "-c", "0" if workers == 1 else "0,2,4,6,8,10", str(runner),
               "--asset-root", str(root / "build/benchmark/assets"), "--scenario", scenario, "--dt", str(1/120),
               "--seed", "0", "--warmup-frames", str(warmup), "--measure-frames", str(steps),
               "--workers", str(workers), "--repetitions", str(repetitions), "--run-index", str(trial),
               "--csv-output", str(csv_path), *extra_arguments]
    if validation:
        command.append("--validate-invariants")
    record = {"case": case, "variant": variant, "trial": trial, "workers": workers, "validation": validation,
              "warmup": warmup, "steps": steps,
              "command": shlex.join(["env", *(f"{key}={value}" for key,value in env.items()), *command]),
              "samples": [raw.name, probe.name]}
    report["runs"].append(record)
    save()
    cap_env = {k: v for k, v in os.environ.items() if not k.startswith("PLATFORMATOR_AUDIT_")}
    with logfile.open("w") as log, subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                                    env={**cap_env, **env}) as process:
        try:
            if budget.wait(process):
                raise subprocess.CalledProcessError(process.returncode, command)
        finally:
            if process.poll() is None:
                process.kill()
    metadata = json.loads(manifest.read_text())
    assert metadata["build"]["commit"] == report["base_commit"] and metadata["build"]["configuration"] == "Release"
    assert metadata["measured_steps"] == steps and metadata["workers"] == workers and metadata["complete_capture"]
    assert metadata["timing_eligible"] != validation
    measured = read_csv(raw)
    sampled = read_csv(probe)
    assert len(measured) == steps and len(sampled) == steps + warmup
    sampled = sampled[warmup:]
    assert all(int(row["step"]) == warmup + i for i, row in enumerate(sampled, 1))
    if variant == "graph":
        assert any(int(row["largest_island"]) > 0 for row in sampled)
    sums = [{
        **{k: int(v) for k, v in row.items() if k not in ("schema_version", "sample_kind", "index")},
        **{k: int(v) for k, v in detail.items() if k != "step"},
        "sync_total_ns": sum(int(detail[x]) for x in ("sync_pre_ns", "sync_post_ns", "sync_cleanup_ns")),
    } for row, detail in zip(measured, sampled)]
    record["build"] = metadata["build"]
    record["workload"] = metadata["resolved_parameters"]
    record["mean"] = {key: stats.mean(row[key] for row in sums) for key in sums[0]}
    record["p95_step_ns"] = sorted(row["complete_step_ns"] for row in sums)[(len(sums) * 95 + 99)//100 - 1]
    counters = [[v for k, v in row.items() if k.endswith("_count")] for row in sums]
    record["counter_sha256"] = hashlib.sha256(json.dumps(counters).encode()).hexdigest()
    if validation:
        snapshots = metadata["physical_invariants"]
        record["invariants"] = {k: snapshots[k] for k in ("finite", "populations_valid", "supports_valid")}
        record["snapshot_sha256"] = hashlib.sha256(json.dumps(snapshots, sort_keys=True).encode()).hexdigest()
        manifest.unlink()
    logfile.unlink()
    save()
    print(case, variant, trial, round(record["mean"]["complete_step_ns"]/1000, 1), "us/step", flush=True)
    return record, sums


def main():
    global output, budget, runner, report
    parser = argparse.ArgumentParser(description="Reproduce the completed physics parallelism audit checks.")
    parser.add_argument("--output", type=Path, default=tools.parent / "evidence" / "reproduction",
                        help="New directory for the report and samples")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    budget = CaptureBudget([output])
    report = {
        "complete": False,
        "base_commit": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
        "host": subprocess.check_output(["lscpu"], text=True),
        "budget": {"seconds": 600, "output_mib": 50},
        "runs": [], "timings": {}, "topology": {}, "validation": {},
    }
    try:
        runner, report["build_commands"] = build_audit_runner()
        report["binary"] = {"path": str(runner), "sha256": digest(runner)}
        report["tools"] = {name: digest(tools / name) for name in (
            "audit.h", "solver.inc", "prepare.py", "run.py", "diagnose.py")}
        save()
        results = {"serial": [], "short": []}
        for trial in range(3):
            order = ("short", "serial") if trial % 2 else ("serial", "short")
            for variant in order:
                record, _ = run("trigger_dense", variant, trial, repetitions=3)
                results[variant].append(record)
        assert len({record["counter_sha256"] for runs in results.values() for record in runs}) == 1
        report["timings"]["trigger_dense"] = {
            variant: {**{key: stats.median(row["mean"][key] for row in runs) for key in runs[0]["mean"]},
                      "p95_step_ns": stats.median(row["p95_step_ns"] for row in runs)}
            for variant, runs in results.items()
        }
        run("single_container", "serial", 0)
        collect_diagnostics(run, report, save)
        report["complete"] = True
        save()
    except BaseException as error:
        report["failure"] = repr(error)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        raise


if __name__ == "__main__":
    main()
