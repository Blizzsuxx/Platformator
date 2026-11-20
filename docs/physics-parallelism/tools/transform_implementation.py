"""Profile simulation-side transform synchronization with a live render cache."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess

from compare_implementation import archive, build, root
from capture_budget import CaptureBudget
from prepare import build_audit_runner, once, tools

staged = """
    if (transform_measurement::mode == "current")
    {
        for (auto *object : dirtyTransforms)
            if (!hasDirtyAncestor(object))
                synchronizeSubtree(object);
    }
    else
    {
        struct Change
        {
            GameObject *object;
            Eigen::Vector2f position;
        };
        static thread_local std::vector<GameObject *> rootStorage;
        static thread_local std::vector<std::vector<Change>> changeStorage;
        auto &roots = rootStorage;
        auto &changes = changeStorage;
        roots.clear();
        for (auto *object : dirtyTransforms)
            if (!hasDirtyAncestor(object))
                roots.push_back(object);
        changes.resize(roots.size());
        const auto beginWalk = transform_measurement::Clock::now();
        const auto compute = [&](size_t index) {
            const auto visit = [](auto &&self, GameObject *object, const Eigen::Vector2f &world,
                                  std::vector<Change> &list) -> void {
                list.push_back({object, world});
                for (auto *child : object->children)
                    self(self, child, (world + child->position).eval(), list);
            };
            auto *root = roots[index];
            auto &list = changes[index];
            list.clear();
            visit(visit, root, root->parent ? (root->parent->worldPosition + root->position).eval() : root->position,
                  list);
        };
        if (transform_measurement::mode == "staged-parallel")
            tbb::parallel_for(size_t{0}, roots.size(), compute);
        else
            for (size_t i = 0; i < roots.size(); ++i)
                compute(i);
        transform_measurement::sample.walk += transform_measurement::elapsed(beginWalk);

        const auto beginPublication = transform_measurement::Clock::now();
        for (const auto &list : changes)
        {
            for (const auto &change : list)
            {
                auto *object = change.object;
                const bool moved = object->worldPosition != change.position;
                object->worldPosition = change.position;
                object->immediateWorldPosition = change.position;
                if (moved)
                {
                    publishRenderGeometryChange(object);
                    if (auto *body = object->getComponent<Rigidbody>())
                        body->wakeUp();
                    object->updateCollider();
                }
            }
            PLATFORMATOR_WORK_ADD(transformVisits, list.size());
        }
        transform_measurement::sample.publish += transform_measurement::elapsed(beginPublication);
    }
"""


def profile(text):
    text = '#include "transform_measurement.h"\n#include <oneapi/tbb/parallel_for.h>\n' + text
    text = once(text, "    const uint64_t cleanStamp = ++transformStamp;",
                "    transform_measurement::Scope timing;\n    const uint64_t cleanStamp = ++transformStamp;")
    return once(text, """    for (auto *object : dirtyTransforms)
    {
        if (!hasDirtyAncestor(object))
        {
            synchronizeSubtree(object);
        }
    }
""", staged)


def main():
    parser = argparse.ArgumentParser(description="Measure simulation-side transform synchronization.")
    parser.add_argument("--output", type=Path, default=archive / "transform-sync",
                        help="New directory for the report and samples")
    output = parser.parse_args().output.resolve()
    if output.exists():
        parser.error(f"output directory already exists: {output}")
    fixture = tools / "transform_fixture.cpp"
    binary, commands = build_audit_runner({"benchmark_main.cpp": lambda _: fixture.read_text(),
                                          "gamemanager.cpp": profile}, build / "transform-runner")
    output.mkdir(parents=True, exist_ok=False)
    budget = CaptureBudget([archive, output])
    report = {"complete": False, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(), "build_commands": commands,
              "workers": 6, "hierarchy": {"roots": 128, "branches_per_root": 4, "sprites_per_branch": 8,
                                          "colliders": 128}, "warmup": 5, "measured": 30, "repetitions": 3,
              "budget": {"seconds": 600, "output_mib": 50}, "runs": []}
    modes = ("current", "staged-serial", "staged-parallel")
    try:
        for trial in range(3):
            for mode in modes[trial:] + modes[:trial]:
                samples = output / f"{mode}-{trial}.csv"
                log = samples.with_suffix(".log")
                settings = {"SDL_AUDIODRIVER": "dummy", "SDL_VIDEODRIVER": "dummy", "SDL_RENDER_DRIVER": "software",
                            "PLATFORMATOR_RENDER_CULLING_MODE": "cached-linear",
                            "PLATFORMATOR_IMPLEMENTATION_TRANSFORMS": mode}
                command = ["taskset", "-c", "0,2,4,6,8,10", str(binary), str(root / "build/benchmark/assets"), str(samples)]
                run = {"mode": mode, "trial": trial, "raw": samples.name,
                       "command": shlex.join(["env", *(f"{k}={v}" for k, v in settings.items()), *command])}
                report["runs"].append(run)
                with log.open("w") as stream, subprocess.Popen(command, env={**os.environ, **settings},
                                                               stdout=stream, stderr=subprocess.STDOUT) as process:
                    try:
                        if budget.wait(process):
                            raise subprocess.CalledProcessError(process.returncode, command)
                    finally:
                        if process.poll() is None:
                            process.kill()
                fingerprint = log.read_text().strip().splitlines()[-1]
                assert len(fingerprint) == 64 and all(c in "0123456789abcdef" for c in fingerprint)
                run["source_sha256"] = fingerprint
                with samples.open() as stream:
                    rows = list(csv.DictReader(stream))
                assert len(rows) == 30 and all(int(row["sync_ns"]) > 0 for row in rows)
                run["means"] = {name: statistics.mean(int(row[name]) for row in rows) for name in rows[0]}
                log.unlink()
        report["summary"] = {mode: {key: statistics.median(
            run["means"][key] for run in report["runs"] if run["mode"] == mode)
            for key in report["runs"][0]["means"]} for mode in modes}
        report["complete"] = True
    except BaseException as error:
        report["failure"] = repr(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        budget.check()


if __name__ == "__main__":
    main()
