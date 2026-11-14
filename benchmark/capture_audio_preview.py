#!/usr/bin/env python3
"""Capture a compact, bounded audio preview startup and mixer-output pilot."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY_CANDIDATES = (
    ROOT / "build/platformator_editor_audiopreview_test",
    ROOT / "bin/editor-debug/platformator_editor_audiopreview_test",
)
OUTPUT_LIMIT_BYTES = 64 * 1024
WALL_BUDGET_SECONDS = 60
LATENCY_LIMIT_MS = 250.0
FIXTURES = {
    "wav": ("pcm_s16le", "wav"),
    "ogg": ("libvorbis", "ogg"),
    "mp3": ("libmp3lame", "mp3"),
    "flac": ("flac", "flac"),
}
CSV_HEADER = (
    "format,stage,sample,submission_ms,first_mixer_output_ms,"
    "submission_succeeded,mixer_output_delivered\n"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def summarize(rows: list[dict[str, str]], stage: str, field: str) -> dict[str, float | int]:
    values = [
        float(row[field]) for row in rows
        if row.get("stage") == stage and row.get(field) and
        row.get("submission_succeeded") == "true"
    ]
    if not values:
        return {"count": 0, "median_ms": 0.0, "max_ms": 0.0}
    return {
        "count": len(values),
        "median_ms": round(statistics.median(values), 3),
        "max_ms": round(max(values), 3),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    existing_defaults = [path for path in DEFAULT_BINARY_CANDIDATES if path.is_file()]
    default_binary = max(existing_defaults, key=lambda path: path.stat().st_mtime_ns) \
        if existing_defaults else DEFAULT_BINARY_CANDIDATES[0]
    parser.add_argument("--binary", type=Path, default=default_binary)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()

    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"test executable not found: {binary}")
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        parser.error("ffmpeg is required to create temporary WAV/OGG/MP3/FLAC fixtures")

    run_stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact_dir = args.output_dir or ROOT / "benchmark/results" / f"audio-preview-{run_stamp}"
    artifact_dir = artifact_dir.resolve()
    if artifact_dir.exists():
        parser.error(f"refusing to replace an existing artifact directory: {artifact_dir}")
    artifact_dir.mkdir(parents=True, exist_ok=False)

    relative_binary = os.path.relpath(binary, ROOT)
    started = time.monotonic()
    deadline = started + WALL_BUDGET_SECONDS
    fixture_commands: list[list[str]] = []
    fixture_identities: dict[str, dict[str, str | int]] = {}
    rows: list[dict[str, str]] = []
    raw_csv = CSV_HEADER
    stderr = ""
    failure: str | None = None
    return_code: int | None = None
    wall_seconds = 0.0

    environment = os.environ.copy()
    environment["SDL_AUDIODRIVER"] = "dummy"
    environment["SDL_VIDEODRIVER"] = "dummy"
    measurement_command = [relative_binary, "--timing-capture", "$TEMP_FIXTURE_DIR"]
    with tempfile.TemporaryDirectory(prefix="platformator-audio-pilot-") as temporary:
        fixture_dir = Path(temporary)
        try:
            for name, (codec, container) in FIXTURES.items():
                output = fixture_dir / f"{name}.{container}"
                command = [
                    ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
                    "-ac", "1", "-ar", "22050", "-c:a", codec, str(output),
                ]
                fixture_commands.append(command)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("fixture generation exceeded the 60 second wall budget")
                generated = subprocess.run(
                    command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                    text=True, timeout=min(15.0, remaining), check=False,
                )
                if generated.returncode != 0 or not output.is_file():
                    raise RuntimeError(
                        f"could not generate {name.upper()} fixture: " +
                        generated.stderr.strip()[:1000]
                    )
                fixture_identities[name] = {
                    "bytes": output.stat().st_size,
                    "sha256": sha256(output),
                    "codec": codec,
                }

            command = [relative_binary, "--timing-capture", str(fixture_dir)]
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("fixture generation exhausted the 60 second wall budget")
            completed = subprocess.run(
                command, cwd=ROOT, env=environment, text=True,
                capture_output=True, timeout=remaining, check=False,
            )
            raw_csv = completed.stdout
            stderr = completed.stderr
            return_code = completed.returncode
        except subprocess.TimeoutExpired as error:
            partial_stdout = error.stdout or CSV_HEADER
            partial_stderr = error.stderr or ""
            if isinstance(partial_stdout, bytes):
                partial_stdout = partial_stdout.decode("utf-8", errors="replace")
            if isinstance(partial_stderr, bytes):
                partial_stderr = partial_stderr.decode("utf-8", errors="replace")
            raw_csv = partial_stdout[:OUTPUT_LIMIT_BYTES]
            stderr = partial_stderr[:2000]
            failure = f"capture exceeded the {WALL_BUDGET_SECONDS} second wall budget"
            return_code = None
        except (OSError, RuntimeError, TimeoutError) as error:
            stderr = str(error)[:2000]
            failure = str(error)
            raw_csv = CSV_HEADER
        finally:
            wall_seconds = time.monotonic() - started

    stdout_bytes = raw_csv.encode("utf-8")
    stderr_bytes = stderr.encode("utf-8")
    if len(stdout_bytes) + len(stderr_bytes) > OUTPUT_LIMIT_BYTES:
        raw_csv = stdout_bytes[:OUTPUT_LIMIT_BYTES].decode("utf-8", errors="ignore")
        stderr = "capture exceeded the 64 KiB output bound"
        failure = stderr
    rows = list(csv.DictReader(io.StringIO(raw_csv)))
    submission_values = [
        float(row["submission_ms"]) for row in rows if row.get("submission_ms")
    ]
    output_values = [
        float(row["first_mixer_output_ms"]) for row in rows
        if row.get("first_mixer_output_ms")
    ]
    all_succeeded = len(rows) == 40 and all(
        row.get("submission_succeeded") == "true" and
        row.get("mixer_output_delivered") == "true"
        for row in rows
    )
    max_submission_ms = max(submission_values, default=0.0)
    max_output_ms = max(output_values, default=0.0)
    passed = (
        failure is None and return_code == 0 and all_succeeded and
        max_submission_ms <= LATENCY_LIMIT_MS and max_output_ms <= LATENCY_LIMIT_MS
    )
    try:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
            capture_output=True, timeout=5, check=False,
        ).stdout.strip()
    except subprocess.TimeoutExpired:
        revision = "unavailable"

    (artifact_dir / "samples.csv").write_text(raw_csv, encoding="utf-8")
    report = {
        "schema": "platformator.audio-preview-timing.v2",
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "workload": {
            "formats": fixture_identities,
            "duration_seconds": 2,
            "cold_samples_per_format": 5,
            "warmup_samples_per_format": 3,
            "warm_samples_per_format": 5,
            "audio_backend": "SDL dummy playback device",
            "synchronous_measurement": "toggle_or_play submission call duration",
            "mixer_output_measurement": "elapsed time until MIX_GetTrackPlaybackPosition reports a decoded frame",
            "physical_audible_output": "UNVERIFIED; a dummy device does not establish sound delivery to a listener",
            "maximum_allowed_submission_and_mixer_output_ms": LATENCY_LIMIT_MS,
        },
        "build": {
            "git_revision": revision,
            "executable": relative_binary,
            "executable_sha256": sha256(binary),
        },
        "command": ["python3", "benchmark/capture_audio_preview.py", "--binary", relative_binary],
        "fixture_commands": [
            ["ffmpeg", *command[1:-1], "$TEMP_FIXTURE_DIR/" + Path(command[-1]).name]
            for command in fixture_commands
        ],
        "measurement_command": measurement_command,
        "environment": {"SDL_AUDIODRIVER": "dummy", "SDL_VIDEODRIVER": "dummy"},
        "process": {
            "return_code": return_code,
            "wall_seconds": round(wall_seconds, 3),
            "wall_budget_seconds": WALL_BUDGET_SECONDS,
            "output_budget_bytes": OUTPUT_LIMIT_BYTES,
            "failure": failure,
        },
        "timing": {
            "cold_submission": summarize(rows, "cold", "submission_ms"),
            "warm_submission": summarize(rows, "warm", "submission_ms"),
            "cold_first_mixer_output": summarize(rows, "cold", "first_mixer_output_ms"),
            "warm_first_mixer_output": summarize(rows, "warm", "first_mixer_output_ms"),
            "maximum_submission_ms": round(max_submission_ms, 3),
            "maximum_first_mixer_output_ms": round(max_output_ms, 3),
        },
        "status": "PASS" if passed else "FAIL_OR_PARTIAL",
        "diagnostic": stderr.strip()[:2000],
    }
    (artifact_dir / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(artifact_dir.relative_to(ROOT))
    print(
        f"status={report['status']} samples={len(rows)} "
        f"submission_max_ms={report['timing']['maximum_submission_ms']} "
        f"mixer_output_max_ms={report['timing']['maximum_first_mixer_output_ms']}"
    )
    if not passed:
        print(stderr.strip() or "audio timing capture did not satisfy the bounded pilot", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
