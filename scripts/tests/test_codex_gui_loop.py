"""Offline orchestration checks; never invokes a model or the real Codex CLI."""

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import time
import unittest
import signal
import io
from contextlib import redirect_stdout
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "codex_gui_loop.py"
SPEC = importlib.util.spec_from_file_location("codex_gui_loop", SCRIPT)
loop = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(loop)


class LoopTests(unittest.TestCase):
    def test_task_parser_preserves_ids_and_checkbox_semantics(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tasks.md"
            path.write_text("- [X] T001 Done\n- [ ] T002 Open\n- [x] T003 Done\n")
            self.assertEqual(loop.tasks(path), {"T001": True, "T002": False, "T003": True})
            path.write_text("- [X] T001 Done\n- [ ] T001 Duplicate\n")
            with self.assertRaises(RuntimeError):
                loop.tasks(path)
        with self.assertRaises(RuntimeError):
            loop.validate_transition({"T001": False}, {"T002": True})
        with self.assertRaises(RuntimeError):
            loop.validate_transition({"T001": True}, {"T001": False})
        loop.validate_implementation(b"- [ ] T001 Work\n", b"- [X] T001 Work\n")
        with self.assertRaises(RuntimeError):
            loop.validate_implementation(b"- [ ] T001 Work\n", b"- [X] T001 Fake\n")

    def test_implementation_can_update_status_without_rewriting_tasks(self):
        before = b"**Current state**: Started\n\n## Phase 1\n- [ ] T200 Fix\n- [ ] T201 Reload\n"
        after = b"**Current state**: T200-T201 complete; evidence open\n\n## Phase 1\n- [X] T200 Fix\n- [X] T201 Reload\n"
        loop.validate_implementation(before, after)
        with self.assertRaisesRegex(RuntimeError, "near line"):
            loop.validate_implementation(before, after.replace(b"T200 Fix", b"T200 Different scope"))

    def test_message_relay_handles_partial_events_and_diagnostics(self):
        event = json.dumps({"type": "item.completed", "item": {
            "type": "agent_message", "text": "Working on T200"}}).encode() + b"\n"
        buffer = bytearray()
        output = io.StringIO()
        with redirect_stdout(output):
            loop.relay_messages(buffer, b"not JSON\n" + event[:25], "implement")
            loop.relay_messages(buffer, event[25:], "implement")
        self.assertEqual(output.getvalue(), "[implement] Working on T200\n")
        self.assertFalse(buffer)

    def test_completion_report_is_role_specific(self):
        loop.validate_report({"outcome": "converged", "summary": "clean"}, "converge")
        for report in ({"outcome": "converged", "summary": "clean"},
                       {"outcome": "progress"},
                       {"outcome": "progress", "summary": None}):
            with self.assertRaises(RuntimeError):
                loop.validate_report(report, "implement")

    def test_model_selection_permissions_and_prompt_are_explicit(self):
        cmd = loop.command("codex", "gpt-6-luna", Path("/repo"), Path("schema"),
                           Path("answer"), "xhigh")
        self.assertEqual(cmd[cmd.index("--model") + 1], "gpt-6-luna")
        self.assertIn("--no-daemon", cmd)
        self.assertIn("--ephemeral", cmd)
        self.assertEqual(cmd[cmd.index("--sandbox") + 1], "workspace-write")
        self.assertEqual(cmd[cmd.index("--ask-for-approval") + 1], "never")
        self.assertNotIn("--dangerously-bypass-approvals-and-sandbox", cmd)
        self.assertEqual(cmd[cmd.index("--config") + 1], 'model_reasoning_effort="xhigh"')
        sol = loop.command("codex", "gpt-6.1-sol", Path("/repo"), Path("schema"),
                           Path("answer"), "medium")
        self.assertEqual(sol[sol.index("--config") + 1], 'model_reasoning_effort="medium"')
        self.assertIn("append-only", loop.prompt("converge", Path("/feature"), 8))
        self.assertIn("do not append another task", loop.prompt("converge", Path("/feature"), 8))

    def fake_args(self, root, body, minutes=1):
        fake = root / "fake-codex"
        fake.write_text("#!/usr/bin/env python3\nimport json,sys,time\n" + body)
        fake.chmod(0o700)
        run = root / "run"
        run.mkdir()
        return argparse.Namespace(cli=str(fake), implement_model="luna", review_model="sol",
                                  implement_effort="xhigh", review_effort="medium",
                                  feature=root, batch_tasks=1, stage_minutes=minutes), run

    def test_stage_requires_valid_final_result_and_successful_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, run = self.fake_args(root, "sys.stdin.read()\n"
                "p=sys.argv[sys.argv.index('--output-last-message')+1]\n"
                "open(p,'w').write(json.dumps({'outcome':'progress','summary':'one task'}))\n"
                "print('MCP transport diagnostic',file=sys.stderr,flush=True)\n"
                "print(json.dumps({'type':'turn.completed'}))\n")
            with mock.patch.object(loop, "ROOT", root):
                report = loop.run_stage(args, "implement", 1, run, time.monotonic()+10, [0])
                self.assertEqual(report["outcome"], "progress")
                for line in (run / "01-implement.jsonl").read_text().splitlines():
                    self.assertIsInstance(json.loads(line), dict)
                self.assertIn("MCP transport diagnostic", (run / "01-implement.stderr.log").read_text())
                args.cli = "/bin/false"
                with self.assertRaises(RuntimeError):
                    loop.run_stage(args, "implement", 2, run, time.monotonic()+10, [0])

    def test_timeout_stops_child_and_retains_partial_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, run = self.fake_args(root, "sys.stdin.read()\nprint('partial',flush=True)\n"
                                           "time.sleep(60)\n", minutes=0.005)
            started = time.monotonic()
            with mock.patch.object(loop, "ROOT", root):
                with self.assertRaisesRegex(RuntimeError, "time limit"):
                    loop.run_stage(args, "implement", 1, run, started+10, [0])
            self.assertLess(time.monotonic()-started, 5)
            self.assertIn("partial", (run / "01-implement.jsonl").read_text())

    def test_deadline_and_stop_remain_active_after_output_is_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, run = self.fake_args(root, "import os\nsys.stdin.read()\n"
                "os.close(1)\nos.close(2)\ntime.sleep(60)\n", minutes=0.005)
            started = time.monotonic()
            with mock.patch.object(loop, "ROOT", root):
                with self.assertRaisesRegex(RuntimeError, "time limit"):
                    loop.run_stage(args, "implement", 1, run, started+10, [0])
            self.assertLess(time.monotonic()-started, 5)

    def test_expired_deadline_never_launches_a_model(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, run = self.fake_args(root, "raise AssertionError('must not launch')\n")
            with mock.patch.object(loop, "ROOT", root), mock.patch.object(loop.subprocess, "Popen") as launch:
                with self.assertRaisesRegex(RuntimeError, "time limit"):
                    loop.run_stage(args, "implement", 1, run, time.monotonic()-1, [0])
                launch.assert_not_called()

    def test_cleanup_signals_surviving_group_even_after_leader_exits(self):
        child = mock.Mock(pid=43210)
        child.poll.return_value = 0
        child.wait.return_value = 0
        signals = []
        terminated = False

        def kill_group(pid, sig):
            nonlocal terminated
            self.assertEqual(pid, child.pid)
            signals.append(sig)
            if sig == signal.SIGTERM:
                terminated = True
            if sig == 0 and terminated:
                raise ProcessLookupError()

        clock = iter(range(100))
        with mock.patch.object(loop.os, "killpg", side_effect=kill_group), \
             mock.patch.object(loop.time, "monotonic", side_effect=lambda: next(clock)), \
             mock.patch.object(loop.time, "sleep"):
            loop.stop_child(child)
        self.assertIn(signal.SIGINT, signals)
        self.assertIn(signal.SIGTERM, signals)
        child.wait.assert_called()

    def test_source_snapshot_includes_new_files_and_excludes_generated_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            feature = root / "specs/feature"
            feature.mkdir(parents=True)
            (feature / "tasks.md").write_text("- [ ] T001 Work\n")
            (root / "bin").mkdir()
            with mock.patch.object(loop, "ROOT", root):
                before = loop.source_snapshot()
                (root / "bin/generated").write_text("allowed generated output")
                (feature / "tasks.md").write_text("- [ ] T001 Work\n- [ ] T002 New\n")
                loop.validate_convergence_sources(before, loop.source_snapshot(), feature)
                (root / "new.cpp").write_text("unexpected application edit")
                with self.assertRaisesRegex(RuntimeError, "new.cpp"):
                    loop.validate_convergence_sources(before, loop.source_snapshot(), feature)

    def test_stop_file_and_output_budget_stop_the_active_pass(self):
        for stop_file in (True, False):
            with self.subTest(stop_file=stop_file), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                args, run = self.fake_args(root, "sys.stdin.read()\nprint('output',flush=True)\n"
                                               "time.sleep(60)\n")
                if stop_file:
                    control = root / "bin/agent-loop"
                    control.mkdir(parents=True)
                    (control / "STOP").touch()
                used = [0 if stop_file else 50 * 1024 * 1024]
                with mock.patch.object(loop, "ROOT", root):
                    with self.assertRaisesRegex(RuntimeError, "STOP|output limit"):
                        loop.run_stage(args, "implement", 1, run, time.monotonic()+10, used)

    def test_stderr_counts_against_the_same_output_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args, run = self.fake_args(root, "sys.stdin.read()\n"
                "print('stderr output',file=sys.stderr,flush=True)\ntime.sleep(60)\n")
            with mock.patch.object(loop, "ROOT", root):
                with self.assertRaisesRegex(RuntimeError, "output limit"):
                    loop.run_stage(args, "implement", 1, run, time.monotonic()+10,
                                   [50 * 1024 * 1024])

    def test_restart_can_review_completed_batch_before_new_implementation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            feature = root / "specs/012-gui-cleanup"
            feature.mkdir(parents=True)
            for name in ("spec.md", "plan.md"):
                (feature / name).write_text("intent\n")
            taskfile = feature / "tasks.md"
            taskfile.write_text("- [ ] T001 Work\n")
            calls = []

            def stage(args, kind, cycle, *rest):
                calls.append(kind)
                if kind == "implement":
                    taskfile.write_text("- [X] T001 Work\n")
                return {"outcome": "progress" if kind == "implement" or cycle == 1
                        else "converged", "summary": "offline restart"}

            with mock.patch.object(loop, "ROOT", root), \
                 mock.patch.object(loop.shutil, "which", return_value="codex"), \
                 mock.patch.object(loop, "run_stage", side_effect=stage):
                self.assertEqual(loop.main(["--start-with", "converge", "--rounds", "2"]), 0)
            self.assertEqual(calls, ["converge", "implement", "converge"])

    def test_outer_loop_completion_append_guard_and_no_progress(self):
        for mode in ("complete", "rewrite", "stalled", "source", "definition"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                feature = root / "specs/012-gui-cleanup"
                feature.mkdir(parents=True)
                for name in ("spec.md", "plan.md"):
                    (feature / name).write_text("intent\n")
                taskfile = feature / "tasks.md"
                taskfile.write_text("- [ ] T001 Work\n")
                calls = []

                def stage(args, stage, cycle, *rest):
                    calls.append(stage)
                    if mode == "complete" and stage == "implement":
                        taskfile.write_text("- [X] T001 Work\n")
                    if mode == "rewrite" and stage == "converge":
                        taskfile.write_text("- [X] T001 Work\n")
                    if mode == "source" and stage == "converge":
                        (root / "new.cpp").write_text("unexpected application edit")
                    if mode == "definition" and stage == "implement":
                        taskfile.write_text("- [X] T001 Changed scope\n")
                    return {"outcome": "progress" if stage == "implement" else "converged",
                            "summary": "offline fixture"}

                with mock.patch.object(loop, "ROOT", root), \
                     mock.patch.object(loop.shutil, "which", return_value="codex"), \
                     mock.patch.object(loop, "run_stage", side_effect=stage):
                    code = loop.main(["--rounds", "3"])
                run = Path((root / "bin/agent-loop/latest.txt").read_text().strip())
                status = json.loads((run / "status.json").read_text())
                if mode == "complete":
                    self.assertEqual(code, 0)
                    self.assertEqual(status["outcome"], "complete")
                    self.assertEqual(status["models"]["implement"], ["gpt-6-luna", "xhigh"])
                    self.assertEqual(status["models"]["converge"], ["gpt-6.1-sol", "medium"])
                    self.assertEqual(calls, ["implement", "converge"])
                else:
                    self.assertEqual(code, 1)
                    self.assertEqual(status["outcome"], "stopped")
                    expected = {"rewrite": "existing task bytes", "source": "outside tasks.md",
                                "definition": "task definitions", "stalled": "without newly completed"}
                    self.assertIn(expected[mode], status["reason"])


if __name__ == "__main__":
    unittest.main()
