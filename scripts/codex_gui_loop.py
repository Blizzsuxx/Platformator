#!/usr/bin/env python3
"""Run sequential Luna implementation / Sol convergence passes on Linux.

Run --dry-run first. Logs/state live in bin/agent-loop/<run>/.
Create bin/agent-loop/STOP to interrupt the active pass and stop the runner.
Remove that file explicitly before starting another run. No model is called by
--dry-run. Normal execution reuses Codex CLI login and consumes account usage.
"""

import argparse
import datetime as dt
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import selectors
import shlex
import shutil
import signal
import subprocess
import sys
import time
from itertools import zip_longest


ROOT = Path(__file__).resolve().parents[1]
TASK = re.compile(r"^- \[([ xX])\] (T\d+)\b", re.MULTILINE)
TASK_MARK = re.compile(rb"^(- \[)[ xX](\] T\d+\b)", re.MULTILINE)
CURRENT_STATE = re.compile(rb"^\*\*Current state\*\*:[^\r\n]*", re.MULTILINE)
OUTCOMES = ("progress", "tasks_appended", "converged", "blocked")
SCHEMA = {
    "type": "object",
    "properties": {
        "outcome": {"type": "string", "enum": list(OUTCOMES)},
        "summary": {"type": "string"},
    },
    "required": ["outcome", "summary"],
    "additionalProperties": False,
}


def tasks(path):
    rows = TASK.findall(path.read_text())
    result = {identity: mark != " " for mark, identity in rows}
    if not rows or len(result) != len(rows):
        raise RuntimeError("tasks.md has no tasks or has duplicate IDs")
    return result


def validate_transition(before, after):
    if not before.keys() <= after.keys():
        raise RuntimeError("A pass removed existing task IDs; stopping for review")
    if any(done and not after[identity] for identity, done in before.items()):
        raise RuntimeError("A pass unchecked completed tasks; stopping for review")


def validate_implementation(before, after):
    # Implementation may maintain this status summary as well as checkboxes.
    # Task descriptions, IDs, phase order and other existing prose stay protected.
    normalized_before = TASK_MARK.sub(rb"\1 \2", CURRENT_STATE.sub(b"**Current state**: <status>", before))
    normalized_after = TASK_MARK.sub(rb"\1 \2", CURRENT_STATE.sub(b"**Current state**: <status>", after))
    if not normalized_after.startswith(normalized_before):
        line = next((i for i, pair in enumerate(zip_longest(
            normalized_before.splitlines(), normalized_after.splitlines()), 1)
                     if pair[0] != pair[1]), 1)
        raise RuntimeError(f"Implementation rewrote existing task definitions or protected text near line {line}; stopping for review")


def relay_messages(buffer, chunk, stage):
    """Echo completed visible agent messages to the top-level launcher log."""
    buffer.extend(chunk)
    while b"\n" in buffer:
        line, _, rest = buffer.partition(b"\n")
        buffer[:] = rest
        try:
            event = json.loads(line)
        except (ValueError, UnicodeDecodeError):
            continue
        if not isinstance(event, dict):
            continue
        item = event.get("item")
        if (event.get("type") == "item.completed" and isinstance(item, dict)
                and item.get("type") == "agent_message" and isinstance(item.get("text"), str)):
            print(f"[{stage}] {item['text']}", flush=True)


def check_controls(deadline):
    if (ROOT / "bin/agent-loop/STOP").exists():
        raise RuntimeError("STOP file requested shutdown")
    if time.monotonic() >= deadline:
        raise RuntimeError("Stage or overnight time limit reached")


def source_snapshot():
    """Read-only content identities; never copy sources or hash generated builds."""
    excluded = {".git", ".aws", ".codex", ".cache", "bin", "build", "__pycache__",
                ".pytest_cache", ".mypy_cache", ".venv", "venv", "node_modules"}
    result = {}
    for directory, children, files in os.walk(ROOT, followlinks=False):
        children[:] = [name for name in children if name not in excluded]
        for name in files + [name for name in children if (Path(directory) / name).is_symlink()]:
            path = Path(directory) / name
            relative = path.relative_to(ROOT)
            if path.is_symlink():
                result[str(relative)] = ("link", os.readlink(path))
            elif path.is_file():
                digest = hashlib.sha256()
                with path.open("rb") as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(chunk)
                result[str(relative)] = ("file", digest.digest())
    return result


def validate_convergence_sources(before, after, feature):
    allowed = str((feature / "tasks.md").relative_to(ROOT))
    changed = sorted(name for name in before.keys() | after.keys()
                     if name != allowed and before.get(name) != after.get(name))
    if changed:
        raise RuntimeError("Convergence changed files outside tasks.md: " + ", ".join(changed[:8]))


def validate_report(report, stage):
    if (not isinstance(report, dict) or set(report) != {"outcome", "summary"}
            or report["outcome"] not in OUTCOMES
            or not isinstance(report["summary"], str)):
        raise RuntimeError("Invalid structured agent result")
    allowed = {"progress", "blocked"} if stage == "implement" else {
        "progress", "tasks_appended", "converged", "blocked"}
    if report["outcome"] not in allowed:
        raise RuntimeError(f"Invalid {stage} outcome: {report['outcome']}")
    return report


def prompt(stage, feature, batch):
    skill = ROOT / ".agents/skills" / f"speckit-{stage}/SKILL.md"
    common = f"""Work in {ROOT}. The feature is {feature}.
Read and apply ${'speckit-' + stage} at {skill} and applicable AGENTS.md.
This is one pass of an unattended sequential Luna implement -> Sol converge loop.
Treat spec.md, plan.md, tasks.md and the constitution as authoritative.
Preserve all existing user changes. Do not edit spec/plan/constitution, this runner,
or its bin/agent-loop control/log files. Do not commit, push, deploy, send external
messages, or delegate to additional agents. Use existing builds under bin/ or build/.
Respect the compact benchmark policy: no expanded/historical campaigns or raised caps.
Missing platform/physical desktop evidence stays unmet; never fabricate completion.
If a required user decision/approval, failing check, or external prerequisite prevents
further permitted work, report blocked with the concrete reason. Do not wait for input.
Your final response must be JSON matching the provided schema, with a brief summary.
"""
    if stage == "implement":
        return common + f"""
Implement the next dependency-coherent batch of at most {batch} unfinished tasks.
Resolve outstanding CRITICAL convergence corrections before dependent integrations.
Read ALL phases, including previous convergence tasks: overlapping obligations need
one correct implementation, not duplicate code. Mark tasks complete only with actual
required validation. You may update the existing **Current state** summary line in
tasks.md, but preserve every existing task description, ID and other existing text.
Take a clean checkpoint at the batch boundary; report progress
even when the whole feature is unfinished. Use blocked only when no independent
permitted task can proceed. Report outcome progress or blocked.
"""
    return common + """
Perform the complete speckit-converge assessment, including tasks regardless of
checkbox state and resulting behavior after corrective chains. Follow its append-only
contract: only append new Convergence work to tasks.md; leave existing bytes intact.
Do not change application code, checkboxes, other artifacts, or use git/history.
For this automated invocation, refine remaining-work recording to prevent task-list
growth from repeated audits: assess ALL scope, but do not append another task for a
gap already fully covered by an existing unchecked task. Reference those open IDs
in your summary instead. Append only newly discovered obligations or residual gaps
in work claimed complete. This recording refinement overrides the skill's instruction
to append a fresh task for every remaining gap on every invocation; all append-only
and assessment constraints still apply. Report progress when only already-recorded
unfinished work remains, never converged in that case.
Report tasks_appended if you append findings, converged only if all specified scope
is implemented and accepted, or blocked if the assessment cannot be completed.
Convergence with remaining unchecked tasks is not completion of this automation.
"""


def command(cli, model, root, schema, answer, effort):
    return [cli, "--no-daemon", "--ask-for-approval", "never", "exec",
            "--cd", str(root), "--sandbox", "workspace-write", "--model", model,
            "--config", f'model_reasoning_effort="{effort}"', "--ephemeral", "--json",
            "--output-schema", str(schema), "--output-last-message", str(answer), "-"]


def stop_child(child):
    # --no-daemon keeps this invocation independent of the shared app server.
    for sig, grace in ((signal.SIGINT, 10), (signal.SIGTERM, 5), (signal.SIGKILL, 5)):
        child.poll()  # Reap an exited leader while checking its surviving group.
        try:
            os.killpg(child.pid, sig)
        except ProcessLookupError:
            return
        until = time.monotonic() + grace
        while time.monotonic() < until:
            child.poll()
            try:
                os.killpg(child.pid, 0)
            except ProcessLookupError:
                child.wait()
                return
            time.sleep(0.05)
    child.wait(timeout=1)


def run_stage(args, stage, cycle, directory, deadline, used_bytes):
    model = args.implement_model if stage == "implement" else args.review_model
    effort = args.implement_effort if stage == "implement" else args.review_effort
    stage_deadline = min(deadline, time.monotonic() + args.stage_minutes * 60)
    check_controls(stage_deadline)
    stem = directory / f"{cycle:02d}-{stage}"
    answer = stem.with_suffix(".json")
    text = prompt(stage, args.feature, args.batch_tasks)
    stem.with_suffix(".prompt.txt").write_text(text)
    argv = command(args.cli, model, ROOT, directory / "schema.json", answer, effort)
    print(f"{stage}: {model} ({effort}); log={stem.with_suffix('.jsonl')}; stderr={stem.with_suffix('.stderr.log')}", flush=True)
    with stem.with_suffix(".jsonl").open("wb") as log, stem.with_suffix(".stderr.log").open("wb") as errors:
        child = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, cwd=ROOT, start_new_session=True)
        try:
            child.stdin.write(text.encode())
            child.stdin.close()
            with selectors.DefaultSelector() as selector:
                selector.register(child.stdout, selectors.EVENT_READ, log)
                selector.register(child.stderr, selectors.EVENT_READ, errors)
                messages = bytearray()
                while selector.get_map() or child.poll() is None:
                    check_controls(stage_deadline)
                    if answer.exists() and answer.stat().st_size > 1024 * 1024:
                        raise RuntimeError("Oversized final agent result")
                    if not selector.get_map():
                        time.sleep(0.1)
                        continue
                    for key, _ in selector.select(timeout=0.5):
                        chunk = os.read(key.fileobj.fileno(), 65536)
                        if not chunk:
                            selector.unregister(key.fileobj)
                            continue
                        used_bytes[0] += len(chunk)
                        if used_bytes[0] > 50 * 1024 * 1024:
                            raise RuntimeError("50-MiB cumulative agent-output limit reached")
                        key.data.write(chunk)
                        key.data.flush()
                        if key.fileobj is child.stdout:
                            relay_messages(messages, chunk, stage)
            if child.wait() != 0:
                raise RuntimeError(f"{stage} process failed; inspect {stem}.jsonl")
        finally:
            stop_child(child)
            child.stdin.close()
            child.stdout.close()
            child.stderr.close()
    if not answer.exists() or answer.stat().st_size > 1024 * 1024:
        raise RuntimeError("Missing or oversized final agent result")
    return validate_report(json.loads(answer.read_text()), stage)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--feature", type=Path, default=Path("specs/012-gui-cleanup"))
    parser.add_argument("--implement-model", default="gpt-6-luna")
    parser.add_argument("--review-model", default="gpt-6.1-sol")
    efforts = ("low", "medium", "high", "xhigh", "max")
    parser.add_argument("--implement-effort", choices=efforts, default="xhigh")
    parser.add_argument("--review-effort", choices=efforts, default="medium")
    parser.add_argument("--hours", type=float, default=8)
    parser.add_argument("--rounds", type=int, default=24)
    parser.add_argument("--stage-minutes", type=float, default=60)
    parser.add_argument("--batch-tasks", type=int, default=24)
    parser.add_argument("--start-with", choices=("implement", "converge"), default="implement",
                        help="Use converge to review a completed batch after a stopped run")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if (not all(math.isfinite(v) for v in (args.hours, args.stage_minutes))
            or min(args.hours, args.rounds, args.stage_minutes, args.batch_tasks) <= 0):
        parser.error("Limits must be positive")
    args.feature = (ROOT / args.feature).resolve()
    if not args.feature.is_relative_to(ROOT):
        parser.error("Feature must be inside this repository")
    args.cli = shutil.which("codex")
    if not args.cli:
        parser.error("Install/login to Codex CLI first")
    for name in ("spec.md", "plan.md", "tasks.md"):
        if not (args.feature / name).is_file():
            parser.error(f"Missing {name} in {args.feature}")
    metadata = ROOT / ".specify/feature.json"
    if metadata.exists():
        selected = json.loads(metadata.read_text()).get("feature_directory")
        if selected and (ROOT / selected).resolve() != args.feature:
            parser.error("--feature differs from the active Spec Kit feature; select it first")
    current = tasks(args.feature / "tasks.md")
    if args.dry_run:
        print(f"Feature: {args.feature}; unchecked={sum(not v for v in current.values())}")
        preview = [("implement", args.implement_model, args.implement_effort),
                   ("converge", args.review_model, args.review_effort)]
        if args.start_with == "converge":
            preview.reverse()
        for stage, model, effort in preview:
            print(shlex.join(command(args.cli, model, ROOT, Path("<run>/schema.json"),
                                     Path(f"<run>/{stage}.json"), effort)))
        print(f"Limits: {args.hours} hours, {args.rounds} rounds, {args.batch_tasks} tasks/batch")
        print(f"First stage: {args.start_with}")
        return 0

    base = ROOT / "bin/agent-loop"
    base.mkdir(parents=True, exist_ok=True)
    with (base / "runner.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("Another implementation/convergence runner is active")
        if (base / "STOP").exists():
            parser.error("STOP exists; remove it explicitly to permit another run")
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        directory = base / f"{stamp}-{os.getpid()}"
        directory.mkdir()
        (directory / "schema.json").write_text(json.dumps(SCHEMA))
        (base / "latest.txt").write_text(str(directory) + "\n")
        deadline = time.monotonic() + args.hours * 3600
        used_bytes, stalled = [0], 0
        status = {"outcome": "running", "run_directory": str(directory),
                  "models": {"implement": [args.implement_model, args.implement_effort],
                             "converge": [args.review_model, args.review_effort]}}
        status_path = directory / "status.json"
        def on_termination(_signal, _frame):
            raise KeyboardInterrupt("SIGTERM requested shutdown")

        prior_termination = signal.signal(signal.SIGTERM, on_termination)
        try:
            for cycle in range(1, args.rounds + 1):
                before = tasks(args.feature / "tasks.md")
                reports = []
                stages = ("converge",) if cycle == 1 and args.start_with == "converge" else ("implement", "converge")
                for stage in stages:
                    check_controls(deadline)
                    prior = (args.feature / "tasks.md").read_bytes()
                    prior_tasks = tasks(args.feature / "tasks.md")
                    sources = source_snapshot() if stage == "converge" else None
                    protected = {p: hashlib.sha256(p.read_bytes()).digest() for p in (
                        args.feature / "spec.md", args.feature / "plan.md",
                        ROOT / ".specify/memory/constitution.md", Path(__file__),
                        ROOT / ".specify/feature.json") if p.exists()}
                    status.update(cycle=cycle, stage=stage)
                    status_path.write_text(json.dumps(status, indent=2))
                    report = run_stage(args, stage, cycle, directory, deadline, used_bytes)
                    if any(hashlib.sha256(p.read_bytes()).digest() != digest
                           for p, digest in protected.items()):
                        raise RuntimeError("Protected design artifact changed; stopping for review")
                    if stage == "converge" and not (args.feature / "tasks.md").read_bytes().startswith(prior):
                        raise RuntimeError("Convergence changed existing task bytes; stopping for review")
                    if stage == "converge":
                        validate_convergence_sources(sources, source_snapshot(), args.feature)
                    else:
                        validate_implementation(prior, (args.feature / "tasks.md").read_bytes())
                    validate_transition(prior_tasks, tasks(args.feature / "tasks.md"))
                    reports.append(report)
                    print(json.dumps(report), flush=True)
                    if report["outcome"] == "blocked":
                        raise RuntimeError(report["summary"])
                after = tasks(args.feature / "tasks.md")
                pending = sum(not v for v in after.values())
                completed = [i for i, done in after.items() if done and not before.get(i, False)]
                status.update(pending=pending, newly_completed=completed, reports=reports)
                if not pending and reports[-1]["outcome"] == "converged":
                    status.update(outcome="complete")
                    return 0
                if "implement" in stages:
                    stalled = 0 if completed else stalled + 1
                if stalled >= 2:
                    raise RuntimeError("Two rounds without newly completed tasks; stopping for review")
                status_path.write_text(json.dumps(status, indent=2))
            raise RuntimeError("Round limit reached with acceptance work still open")
        except (Exception, KeyboardInterrupt) as error:
            status.update(outcome="stopped", reason=str(error) or "Interrupted")
            print(f"Stopped: {status['reason']}", file=sys.stderr, flush=True)
            return 1
        finally:
            signal.signal(signal.SIGTERM, prior_termination)
            status_path.write_text(json.dumps(status, indent=2))
            print(f"Status: {status_path}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
