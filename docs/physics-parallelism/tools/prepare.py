import hashlib
import json
from pathlib import Path
import shlex
import subprocess
from concurrent.futures import ThreadPoolExecutor
from tempfile import TemporaryDirectory

tools = Path(__file__).resolve().parent
root = tools.parents[2]
build = root / "build/benchmark"
scratch = root / "build/parallel-physics-audit"
source = root / "engine/src"


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Expected one match for {old!r}, got {text.count(old)}")
    return text.replace(old, new)


def game_manager(text):
    text = '#include "audit.h"\n' + text
    text = once(text, "    PLATFORMATOR_BENCH_FRAME_BEGIN();",
                """    audit::sample.fill(0);
    PLATFORMATOR_BENCH_FRAME_BEGIN();
    auto timedSync = [&](audit::Field timeField, audit::Field sizeField) {
        audit::sample[sizeField] = dirtyTransforms.size();
        if (dirtyTransforms.empty()) return;
        auto begin = audit::now();
        synchronizeTransforms();
        audit::sample[timeField] = audit::elapsed(begin);
    };""")
    text = once(text, "            synchronizeTransforms();\n            physicsManager->checkForCollisions();",
                """            timedSync(audit::SyncPre, audit::PreDirty);
            physicsManager->checkForCollisions();""")
    text = once(text, "            physicsManager->applyMovement(timeDelta);\n            synchronizeTransforms();",
                """            physicsManager->applyMovement(timeDelta);
            timedSync(audit::SyncPost, audit::PostDirty);""")
    text = once(text, "    PLATFORMATOR_BENCH_FRAME_END();",
                "    PLATFORMATOR_BENCH_FRAME_END();\n    audit::samples.rows.push_back(audit::sample);")
    return once(text, """        if (!dirtyTransforms.empty())
        {
            synchronizeTransforms();
        }

        // Old support recipients""", """        if (!dirtyTransforms.empty())
        {
            audit::sample[audit::CleanupDirty] += dirtyTransforms.size();
            auto begin = audit::now();
            synchronizeTransforms();
            audit::sample[audit::SyncCleanup] += audit::elapsed(begin);
        }

        // Old support recipients""")


def physics_manager(text):
    text = '#include "audit.h"\n#include <bit>\n#include <numeric>\n' + text
    text = once(text, "    const auto &pendingPairs", "    auto begin = audit::now();\n    const auto &pendingPairs")
    start = text.index("    for (auto *pair : pendingPairs)\n    {\n        PLATFORMATOR_WORK_ADD")
    end = text.index("\n#if PLATFORMATOR_ENABLE_DEBUG_TOOLS", start)
    commit = text[start:end].removesuffix("    flushSupportDeltas();\n")
    commit = once(commit, "        if (pair->resultHadContact && !pair->getCollision())", """        const auto *contact = pair->getCollision();
        if constexpr (ShortCommit)
        {
            if (pair->solverIndex == SIZE_MAX && !pair->contributedSupportBodyA && !pair->contributedSupportBodyB &&
                ((!pair->resultHadContact && !contact && pair->contactIndex == SIZE_MAX) ||
                 (pair->resultHadContact && contact && pair->contactIndex != SIZE_MAX &&
                  (pair->getObjectA()->getIsTrigger() || pair->getObjectB()->getIsTrigger()))))
            {
                ++skipped;
                continue;
            }
        }
        if (pair->resultHadContact && !contact)""")
    commit = commit.replace("if (!pair->resultHadContact && pair->getCollision())", "if (!pair->resultHadContact && contact)")
    text = text[:start] + """    audit::sample[audit::Geometry] = audit::elapsed(begin);
    begin = audit::now();
    auto reduce = [&]<bool ShortCommit>() {
        size_t skipped = 0;
""" + "".join("    " + line if line.strip() else line for line in commit.splitlines(keepends=True)) + """        audit::sample[audit::Skip] = skipped;
    };
    if (audit::shortCommit)
        reduce.operator()<true>();
    else
        reduce.operator()<false>();
    audit::sample[audit::Commit] = audit::elapsed(begin);

    begin = audit::now();
    flushSupportDeltas();
    audit::sample[audit::SupportFlush] = audit::elapsed(begin);
""" + text[end:]
    text = once(text, "    float inverseTimeDelta = timeDelta", "    auto begin = audit::now();\n    float inverseTimeDelta = timeDelta")
    start = text.index("    for (int iteration = 0; iteration < COLLISION_SOLVER_ITERATIONS")
    end = text.index("\n#if PLATFORMATOR_ENABLE_BENCHMARKS", start)
    return text[:start] + (tools / "solver.inc").read_text() + text[end:]


def benchmark_main(text):
    text = '#include "audit.h"\n' + text
    text = once(text, "const RigidBodyContainerLayout &layout, const bool attachSprites)",
                "const RigidBodyContainerLayout &layout, const bool attachSprites, float x)")
    for literal, expr in {
        '"Container Floor", 0.0f,': '"Container Floor", x,',
        '"Container Ceiling", 0.0f,': '"Container Ceiling", x,',
        '"Container Left Wall", -halfWidth': '"Container Left Wall", x - halfWidth',
        '"Container Right Wall", halfWidth': '"Container Right Wall", x + halfWidth',
    }.items():
        text = once(text, literal, expr)
    start = text.index("    createRigidBodyContainerWalls(runtime, settings, layout, attachSprites);")
    end = text.index("\n}", start)
    block = text[start:end].replace("layout, attachSprites);", "layout, attachSprites, x);", 1)
    block = block.replace("position.x(), position.y()", "x + position.x(), position.y()")
    text = (text[:start] + """    for (int container = 0; container < audit::containers; ++container)
    {
        const float x = static_cast<float>(container) * 2048.0f;
""" + "\n".join("    " + line if line.strip() else line for line in block.splitlines()) + "\n    }" + text[end:])
    text = once(text, "return size_t(options.boxCount) + options.circleCount + 4 + camera;",
                "return (size_t(options.boxCount) + options.circleCount + 4) * audit::containers + camera;")
    return once(text, '{"wall_thickness", settings.wallThickness},', '{"wall_thickness", settings.wallThickness},\n                       {"audit_containers", audit::containers},')


def run(item):
    command, cwd = item
    logged = f"cd {shlex.quote(str(cwd))} && {shlex.join(command)}"
    print(logged, flush=True)
    subprocess.run(command, cwd=cwd, check=True)
    return logged


def verify_base_fingerprint():
    generated = build / "engine/generated"
    manifest = generated / "platformator-source-hashes.manifest"
    entries = manifest.read_text().splitlines()
    if not entries or not entries[-1].startswith("aggregate:"):
        raise RuntimeError(f"Missing aggregate source fingerprint in {manifest}")
    sources, aggregate = entries[:-1], entries[-1].split(":", 1)[1]
    for entry in sources:
        name, expected = entry.rsplit(":", 1)
        path = root / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Benchmark base build source fingerprint differs for {name}")
    expected_aggregate = hashlib.sha256("".join(f"{entry}\n" for entry in sources).encode()).hexdigest()
    if not sources or aggregate != expected_aggregate:
        raise RuntimeError(f"Invalid aggregate source fingerprint in {manifest}")
    header = (generated / "benchmarkbuildconfig.h").read_text().splitlines()
    if f'#define PLATFORMATOR_BENCH_SOURCE_SHA256 "{aggregate}"' not in header:
        raise RuntimeError("Benchmark base build header and source manifest disagree")
    return aggregate


def build_audit_runner(conversions=None, runner=None):
    if conversions is None:
        conversions = {
            "gamemanager.cpp": game_manager,
            "physicsmanager.cpp": physics_manager,
            "benchmark_main.cpp": benchmark_main,
        }
    if not (build / "CMakeCache.txt").is_file():
        raise RuntimeError(f"Configure the Ninja Release benchmark build at {build} first")
    build_commands = [
        run((["cmake", "-S", str(root), "-B", str(build)], root)),
        run((["cmake", "--build", str(build), "--target", "platformator_benchmark_runner", "--parallel", "5"], root)),
    ]
    fingerprint = verify_base_fingerprint()

    database = {Path(row["file"]).name: row for row in json.loads((build / "compile_commands.json").read_text())
                if Path(row["file"]).name in conversions}
    if runner is None:
        runner = scratch / "repro-runner"
    runner.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="reproduction-", dir=runner.parent) as directory:
        temporary = Path(directory)
        commands = []
        for name, transform in conversions.items():
            target = temporary / name
            target.write_text(transform((source / name).read_text()))
            config = database[name]
            command = shlex.split(config["command"])
            command[command.index("-o") + 1] = str(target.with_suffix(".o"))
            command[command.index("-c") + 1] = str(target)
            command[1:1] = ["-I" + str(source), "-I" + str(tools)]
            commands.append((command, config["directory"]))
        with ThreadPoolExecutor(max_workers=3) as pool:
            build_commands.extend(pool.map(run, commands))

        if verify_base_fingerprint() != fingerprint:
            raise RuntimeError("Benchmark base source fingerprint changed while compiling instrumented sources")

        link = next(line for line in reversed(subprocess.check_output(
            ["ninja", "-C", str(build), "-t", "commands", "platformator_benchmark_runner"], text=True).splitlines())
                    if " -o platformator_benchmark_runner " in line)
        link = [arg for arg in shlex.split(link.removeprefix(": && ").removesuffix(" && :"))
                if not arg.startswith("-Wl,--dependency-file=")]
        link[link.index("-o") + 1] = str(runner)
        main_index = next(i for i, arg in enumerate(link) if arg.endswith("/benchmark_main.cpp.o"))
        link[main_index] = str(temporary / "benchmark_main.o")
        library_index = link.index("libplatformator.a")
        link[library_index:library_index] = [str(temporary / Path(name).with_suffix(".o"))
                                             for name in conversions if name != "benchmark_main.cpp"]
        build_commands.append(run((link, build)))
    return runner, build_commands


if __name__ == "__main__":
    build_audit_runner()
