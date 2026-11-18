"""Size/time guard for generated capture output; never deletes partial evidence."""

from contextlib import contextmanager
import math
from pathlib import Path
import subprocess
import time


class BudgetExceeded(RuntimeError):
    pass


class CaptureBudget:
    def __init__(self, roots, max_mib=50, max_seconds=600):
        if not all(math.isfinite(value) and value > 0 for value in (max_mib, max_seconds)):
            raise ValueError("Capture size and time limits must be finite and positive")
        # Nested phase directories count only once. Input evidence/builds are not roots.
        roots = sorted({Path(root).resolve() for root in roots}, key=lambda path: len(path.parts))
        self.roots = [root for root in roots if not any(parent in root.parents for parent in roots)]
        self.max_bytes = int(max_mib * 1024 * 1024)
        self.deadline = time.monotonic() + max_seconds
        self.max_seconds = max_seconds

    def check(self, reserve_bytes=0):
        if time.monotonic() >= self.deadline:
            raise BudgetExceeded(f"Capture exceeded {self.max_seconds:g} seconds; stopped without a success report")
        size = reserve_bytes
        for root in self.roots:
            if not root.exists():
                continue
            if root.is_file() and not root.is_symlink():
                size += root.stat().st_size
                if size > self.max_bytes:
                    raise BudgetExceeded(
                        f"Capture exceeded {self.max_bytes / 1024 / 1024:g} MiB; "
                        "stopped without a success report. Partial files retained for inspection")
                continue
            for path in root.rglob("*"):
                if path.is_file() and not path.is_symlink():
                    size += path.stat().st_size
                    if size > self.max_bytes:
                        raise BudgetExceeded(
                            f"Capture exceeded {self.max_bytes / 1024 / 1024:g} MiB; "
                            "stopped without a success report. Partial files retained for inspection")

    def wait(self, process, reserve_bytes=0):
        while True:
            self.check(reserve_bytes=reserve_bytes)
            try:
                result = process.wait(timeout=min(0.25, max(0.001, self.deadline - time.monotonic())))
            except subprocess.TimeoutExpired:
                continue
            self.check(reserve_bytes=reserve_bytes)
            return result


_active_budget = None


@contextmanager
def capture_budget(budget):
    global _active_budget
    previous = _active_budget
    _active_budget = budget
    try:
        budget.check()
        yield
        budget.check()
    finally:
        _active_budget = previous


def check_budget(reserve_bytes=0):
    if _active_budget is not None:
        _active_budget.check(reserve_bytes=reserve_bytes)


def active_budget_max_bytes():
    return _active_budget.max_bytes if _active_budget is not None else None


def wait_for_process(process, reserve_bytes=0):
    return process.wait() if _active_budget is None else _active_budget.wait(process, reserve_bytes=reserve_bytes)
