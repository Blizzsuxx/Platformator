"""Instrument the current production solver and run paired pilots or same-state replay."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
from tempfile import mkdtemp

from compare_implementation import archive, build, capture, fresh_output
from prepare import benchmark_main, build_audit_runner, once, root

parity = """
    // Run both iterations from the same velocities and cached impulses; preparation is shared.
    if (solver_measurement::mode == "parity" || solver_measurement::mode == "parity-islands")
    {
        struct State
        {
            std::vector<std::array<float, 3>> velocities;
            std::vector<std::array<float, 4>> impulses;
        };
        const auto read = [&] {
            State state;
            for (const auto *body : registeredBodies)
                state.velocities.push_back({body->velocity.x(), body->velocity.y(), body->angularVelocity});
            for (const auto &constraint : solverConstraints)
            {
                std::array<float, 4> impulses{};
                for (size_t i = 0; i < constraint.pointCount; ++i)
                {
                    const auto &point = constraint.contactPoints->points[i];
                    impulses[2 * i] = point.accumulatedNormalImpulse;
                    impulses[2 * i + 1] = point.accumulatedTangentImpulse;
                }
                state.impulses.push_back(impulses);
            }
            return state;
        };
        const auto initial = read();
        solveConstraintsSerial();
        const auto expected = read();
        for (size_t i = 0; i < registeredBodies.size(); ++i)
        {
            const auto &v = initial.velocities[i];
            registeredBodies[i]->velocity = Eigen::Vector2f(v[0], v[1]);
            registeredBodies[i]->angularVelocity = v[2];
        }
        for (size_t j = 0; j < solverConstraints.size(); ++j)
        {
            const auto &constraint = solverConstraints[j];
            for (size_t i = 0; i < constraint.pointCount; ++i)
            {
                auto &point = constraint.contactPoints->points[i];
                point.accumulatedNormalImpulse = initial.impulses[j][2 * i];
                point.accumulatedTangentImpulse = initial.impulses[j][2 * i + 1];
            }
        }
        runParallel();
        const auto actual = read();
        const auto sameBits = [](const auto &a, const auto &b) {
            for (size_t i = 0; i < a.size(); ++i)
            {
                for (size_t j = 0; j < a[i].size(); ++j)
                    if (std::bit_cast<uint32_t>(a[i][j]) != std::bit_cast<uint32_t>(b[i][j])) return false;
            }
            return true;
        };
        if (!sameBits(expected.velocities, actual.velocities) || !sameBits(expected.impulses, actual.impulses))
            throw std::runtime_error("Solver iteration replay disagrees with serial velocities or impulses");
        ++solver_measurement::sample.parityChecks;
    }
    else
    {
        runParallel();
    }
"""


def instrument(text):
    text = '#include "solver_measurement.h"\n' + text
    text = once(text, "    float inverseTimeDelta = timeDelta", """    solver_measurement::sample = {};
    const auto started = solver_measurement::Clock::now();
    float inverseTimeDelta = timeDelta""")
    text = once(text, "    solveConstraintIterations();", """
    solver_measurement::sample.preparation = std::chrono::duration_cast<solver_measurement::Nanoseconds>(
        solver_measurement::Clock::now() - started).count();
    solveConstraintIterations();
    solver_measurement::capture.rows.push_back(solver_measurement::sample);""")
    text = once(text, "    if (solverConstraints.size() >= parallelConstraintThreshold",
                '    if (solver_measurement::mode != "serial" && solverConstraints.size() >= parallelConstraintThreshold')
    text = once(text, "void PhysicsManager::solveConstraintsSerial()\n{", """void PhysicsManager::solveConstraintsSerial()
{
    solver_measurement::Timer timer{solver_measurement::sample.iterations};""")
    start = text.index("bool PhysicsManager::prepareSolverIslands()")
    end = text.index("void PhysicsManager::solveIslands()", start)
    grouping = once(text[start:end], "bool PhysicsManager::prepareSolverIslands()\n{",
                    """bool PhysicsManager::prepareSolverIslands()
{
    solver_measurement::Timer timer{solver_measurement::sample.grouping};""")
    grouping = once(grouping, "    return true;\n}\n", """    solver_measurement::sample.islandCount = solverIslandCount;
    solver_measurement::sample.largestIsland = largestSolverIsland;
    return true;
}
""")
    text = text[:start] + grouping + text[end:]
    text = wrap_dispatch(text, "void PhysicsManager::solveIslands()", "islandsDispatched")
    if "bool PhysicsManager::prepareOrderedSolverBatches()" in text:
        text = once(text, "        if (largestSolverIsland >= orderedConstraintThreshold", """
        if (solver_measurement::mode != "islands-only" && solver_measurement::mode != "parity-islands" &&
            largestSolverIsland >= orderedConstraintThreshold""".lstrip("\n"))
        text = once(text, "bool PhysicsManager::prepareOrderedSolverBatches()\n{", """bool PhysicsManager::prepareOrderedSolverBatches()
{
    solver_measurement::Timer timer{solver_measurement::sample.grouping};""")
        text = wrap_dispatch(text, "void PhysicsManager::solveOrderedBatches()", "batchesDispatched")
    return text


def wrap_dispatch(text, signature, count):
    start = text.index("\n{\n", text.index(signature))
    end = text.index("\n}\n", start)
    original = text[start + 3:end]
    body = f"""\n{{
    ++solver_measurement::sample.{count};
    const auto runParallel = [&] {{
        solver_measurement::Timer timer{{solver_measurement::sample.iterations}};
""" + "\n".join("    " + line if line else "" for line in original.splitlines()) + "\n    };\n"
    return text[:start] + (body + parity).rstrip() + text[end:]


single = ["--scenario", "rigid_body_container", "--box-count", "500", "--circle-count", "500"]
separated = ["--scenario", "rigid_body_container", "--box-count", "63", "--circle-count", "62"]


def validate(binary, folder, budget, scenario, options, dispatch):
    output = folder / "parity.profile.csv"
    log = folder / "parity.log"
    result_path = folder / "parity.json"
    mode = "parity-islands" if dispatch == "islands_dispatched" else "parity"
    environment = {"PLATFORMATOR_IMPLEMENTATION_MODE": mode,
                   "PLATFORMATOR_IMPLEMENTATION_PROFILE": str(output), **options}
    command = ["taskset", "-c", "0,2,4,6,8,10", str(binary), "--asset-root", str(root / "build/benchmark/assets"),
               "--workers", "6", "--seed", "0", "--dt", str(1/120), "--warmup-frames", "120",
               "--measure-frames", "600", "--validate-invariants", *scenario]
    record = {"command": shlex.join(["env", *(f"{k}={v}" for k, v in environment.items()), *command]),
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    try:
        with log.open("w") as stream, subprocess.Popen(command, env={**os.environ, **environment},
                                                       stdout=stream, stderr=subprocess.STDOUT) as process:
            try:
                if budget.wait(process):
                    raise subprocess.CalledProcessError(process.returncode, command)
            finally:
                if process.poll() is None:
                    process.kill()
        with output.open() as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == 720
        exercised = sum(int(row[dispatch]) for row in rows)
        assert exercised > 0
        assert all(int(row["parity_checks"]) == int(row["islands_dispatched"]) + int(row["batches_dispatched"])
                   for row in rows)
        record["exact_parallel_replays"] = exercised
        record["physical_invariants"] = True
        output.unlink()
        log.unlink()
    except BaseException as error:
        record["failure"] = repr(error)
        raise
    finally:
        result_path.write_text(json.dumps(record, indent=2) + "\n")
        budget.check()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=["islands", "ordered-batches"])
    parser.add_argument("--output", type=Path, help="New directory for the report and samples")
    args = parser.parse_args()
    folder = fresh_output(parser, args.output or archive / f"solver-{args.stage}-reproduction")
    try:
        build.mkdir(parents=True, exist_ok=True)
        runner = Path(mkdtemp(prefix=f"{args.stage}-", dir=build)) / "runner"
        binary, commands = build_audit_runner({"benchmark_main.cpp": benchmark_main, "physicsmanager.cpp": instrument},
                                             runner)
    except Exception as error:
        (folder / "report.json").write_text(json.dumps(
            {"complete": False, "phase": "build", "failure": repr(error)}, indent=2) + "\n")
        raise

    def variant(mode):
        return binary, {"PLATFORMATOR_IMPLEMENTATION_PROFILE": "", "PLATFORMATOR_IMPLEMENTATION_MODE": mode}
    if args.stage == "islands":
        separated_env = {"PLATFORMATOR_AUDIT_PILES": "1"}
        capture(folder, [("eight-containers", separated, False, separated_env), ("single-container", single, False, {})],
                {"serial": variant("serial"), "parallel": variant("islands-only")}, extra={"build_commands": commands},
                precheck=lambda output, budget: validate(binary, output, budget, separated, separated_env,
                                                        "islands_dispatched"))
    else:
        capture(folder, [("single-container", single, False, {})],
                {"islands-only": variant("islands-only"), "parallel-batches": variant("parallel")},
                extra={"build_commands": commands},
                precheck=lambda output, budget: validate(binary, output, budget, single, {}, "batches_dispatched"))


if __name__ == "__main__":
    main()
