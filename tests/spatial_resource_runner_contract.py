#!/usr/bin/env python3
"""Smoke the resource-only runner and verify timing-runner separation."""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "benchmark"))
from spatial_results import read_schema4_capture  # noqa: E402


def main():
    resource_runner, timing_runner, scratch_root = map(Path, sys.argv[1:4])
    scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="spatial-resource-contract-", dir=scratch_root) as temporary:
        output = Path(temporary) / "resource.csv"
        command = [str(resource_runner), "--scenario", "broad_phase", "--broad-count", "100",
                   "--broad-phase", "bvh", "--render-culling", "none", "--workers", "1",
                   "--warmup-frames", "0", "--measure-frames", "1", "--seed", "0",
                   "--capture-mode", "resource", "--csv-output", str(output)]
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode:
            raise RuntimeError(f"resource runner failed ({completed.returncode}):\n{completed.stdout}\n{completed.stderr}")
        capture = read_schema4_capture(output)
        manifest = capture["manifest"]
        resources = manifest["resources"]
        if manifest["capture_mode"] != "resource" or manifest["timing_eligible"] is not False:
            raise AssertionError("resource capture is not marked timing-ineligible")
        if manifest["build"]["resource_hooks"] is not True or resources["status"] != "complete":
            raise AssertionError("resource capture is missing hooks or measurements")
        if "box2d" not in resources["domains"] or "scratch" not in resources["domains"]:
            raise AssertionError("resource allocation domains are incomplete")
        physics = resources["domains"]["physics_index"]
        if physics["peak_owned_capacity_bytes"] <= 0 or physics["peak_live_bytes"] <= 0:
            raise AssertionError("physics BVH capacity and allocation bytes were not accounted")

        trace = Path(temporary) / "collision.jsonl"
        trace.write_text(
            '{"type":"header","format":"platformator-spatial-trace","version":1,'
            '"domain":"collision","case_id":"resource-capacity-smoke","seed":0,'
            '"units":"world","dt":0.008333333333333333,"warmup_steps":1,'
            '"measure_steps":2,"generator":{"name":"resource-contract","revision":"1",'
            '"source_hash":"' + "a" * 64 + '"},"parameters":{"population":2,'
            '"bounds_source":"provided-aabb"}}\n'
            '{"type":"initial","colliders":[{"id":{"slot":"1","generation":"1"},'
            '"bounds":[0,0,2,2],"group":"1","mask":"1","active":true},'
            '{"id":{"slot":"2","generation":"1"},"bounds":[1,1,3,3],'
            '"group":"1","mask":"1","active":true}]}\n'
            '{"type":"step","index":0,"ops":[]}\n'
            '{"type":"step","index":1,"ops":[]}\n'
            '{"type":"step","index":2,"ops":[]}\n',
            encoding="utf-8",
        )
        replay_output = Path(temporary) / "resource-replay.csv"
        replay = subprocess.run(
            [str(resource_runner), "--spatial-trace", str(trace), "--broad-phase", "bvh",
             "--render-culling", "none", "--workers", "1", "--capture-mode", "resource",
             "--csv-output", str(replay_output)],
            text=True, capture_output=True,
        )
        if replay.returncode:
            raise RuntimeError(f"resource replay failed ({replay.returncode}):\n{replay.stdout}\n{replay.stderr}")
        replay_manifest = read_schema4_capture(replay_output)["manifest"]
        replay_resources = replay_manifest["resources"]
        history = replay_resources.get("owned_capacity_snapshots", {})
        if history.get("status") != "complete" or history.get("sampling") != \
                "after initialization and each complete replay step, outside measured intervals":
            raise AssertionError("replay resource capacity sampling is not declared complete")
        replay_index = replay_resources["domains"]["physics_index"]
        replay_pairs = replay_resources["domains"]["physics_common_pairs"]
        if (replay_index["initial_owned_capacity_bytes"] <= 0 or
                replay_index["peak_owned_capacity_bytes"] < replay_index["initial_owned_capacity_bytes"] or
                replay_index["final_owned_capacity_bytes"] <= 0 or
                replay_pairs["initial_owned_capacity_bytes"] <= 0):
            raise AssertionError("replay index and common-pair owned capacities were not sampled")

        rejected = subprocess.run([str(timing_runner), "--scenario", "broad_phase", "--broad-count", "1",
                                   "--capture-mode", "resource"], text=True, capture_output=True)
        diagnostic = rejected.stdout + rejected.stderr
        if rejected.returncode == 0 or "resource runner" not in diagnostic.lower():
            raise AssertionError("normal timing runner accepted resource capture mode")
        print("resource runner produced a complete schema-4 capture; timing runner rejected resource mode")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
