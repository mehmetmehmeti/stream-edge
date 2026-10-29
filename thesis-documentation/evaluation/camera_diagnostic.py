"""Measure direct V4L2 capture from driver timestamps; discard image data."""
import argparse
import datetime
import json
from pathlib import Path
import re
import signal
import subprocess
import time


def frame_metrics(log, warmup, expected_bytes=None):
    buffers = []
    for line in log.splitlines():
        match = re.search(r"cap dqbuf:.*?seq:\s*(\d+).*?ts:\s*([\d.]+)", line)
        if not match:
            continue
        size = re.search(r"bytesused:\s*(\d+)", line)
        short = expected_bytes is not None and (size is None or int(size[1]) != expected_bytes)
        buffers.append((int(match[1]), float(match[2]), bool(re.search(r"\berror\b", line)), short))
    if len(buffers) < 2:
        raise ValueError("Fewer than two driver frame timestamps; inspect capture.log")
    errors = sum(buffer[2] for buffer in buffers)
    short = sum(buffer[3] for buffer in buffers)
    selected = [(sequence, timestamp) for sequence, timestamp, error, truncated in buffers
                if not error and not truncated and timestamp >= buffers[0][1] + warmup]
    if len(selected) < 2:
        raise ValueError(f"Insufficient valid frames after warmup: {len(buffers)} buffers, {errors} driver errors, {short} incomplete buffers")
    intervals = [b[1] - a[1] for a, b in zip(selected, selected[1:])]
    if any(interval <= 0 for interval in intervals):
        raise ValueError("Driver timestamps are not strictly increasing")
    if any(b[0] <= a[0] for a, b in zip(selected, selected[1:])):
        raise ValueError("Driver sequence reset during measurement")
    duration = selected[-1][1] - selected[0][1]
    return {"captured_buffers": len(buffers), "measured_buffers": len(selected),
            "driver_error_buffers": errors, "incomplete_buffers": short,
            "warmup_s": warmup, "driver_interval_s": duration,
            "driver_first_timestamp_s": selected[0][1], "driver_last_timestamp_s": selected[-1][1],
            "observed_fps": (len(selected) - 1) / duration,
            "minimum_frame_interval_ms": min(intervals) * 1000,
            "maximum_frame_interval_ms": max(intervals) * 1000,
            "driver_sequence_gaps": sum(b[0] - a[0] - 1 for a, b in zip(selected, selected[1:]))}


def settings(device):
    return subprocess.check_output(["v4l2-ctl", "--device", device, "--all"], text=True)


def controls(log):
    return {name: int(value) for name, value in re.findall(
        r"^\s*(auto_exposure|exposure_dynamic_framerate)\s+.*?\bvalue=(-?\d+)", log, re.MULTILINE)}


def capture(args):
    args.output.mkdir(parents=True, exist_ok=False)
    before = settings(args.device)
    (args.output / "settings-before.txt").write_text(before)
    command = ["v4l2-ctl", "--device", args.device, "--verbose", "--stream-mmap=4",
               "--stream-count=100000", "--stream-to=/dev/null"]
    started_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
    started = time.monotonic()
    child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        try:
            log, _ = child.communicate(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            child.send_signal(signal.SIGINT)
            try:
                log, _ = child.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                log, _ = child.communicate()
                raise RuntimeError("Capture did not stop after SIGINT")
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    elapsed = time.monotonic() - started
    (args.output / "capture.log").write_text(log)
    try:
        after = settings(args.device)
        settings_error = None
    except subprocess.CalledProcessError:
        after = ""
        settings_error = "Camera settings unavailable after capture; device may have disconnected"
    (args.output / "settings-after.txt").write_text(after)
    result = {"label": args.label, "started_utc": started_utc,
              "command": command, "wall_elapsed_s": elapsed, "capture_returncode": child.returncode,
              "controls_before": controls(before), "controls_after": controls(after),
              "status": "failed", "metrics_valid": False,
              "notes": ["Direct camera-driver buffers; no Qt, Holoscan, encoder, recorder or network.",
                        "FPS=(measured_buffers-1)/(last-first driver timestamp).",
                        "Image bytes discarded; log contains timestamps and buffer metadata only.",
                        "Driver sequence gaps are not total camera-to-viewer frame loss.",
                        "Error-flagged/incomplete buffers are excluded; gaps between accepted sequences can include rejected frames.",
                        "This script does not set exposure, frame rate or pixel format."]}
    try:
        if settings_error:
            raise ValueError(settings_error)
        if child.returncode not in (0, -signal.SIGINT, 128 + signal.SIGINT):
            raise ValueError(f"v4l2-ctl exited {child.returncode}; inspect capture.log")
        raw_yuyv = re.search(r"Pixel Format\s*:\s*'YUYV'", before)
        size = re.search(r"Size Image\s*:\s*(\d+)", before)
        result.update(frame_metrics(log, args.warmup, int(size[1]) if raw_yuyv and size else None))
        if result["driver_interval_s"] < args.seconds - args.warmup - 3:
            raise ValueError("Valid frame timestamps do not cover the requested measurement interval")
        result.update(status="measured", metrics_valid=True)
    except ValueError as error:
        result.update(status="failed", metrics_valid=False, observed_fps=None, error=str(error))
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not result["metrics_valid"]:
        raise RuntimeError(result["error"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="/dev/video0")
    parser.add_argument("--seconds", type=float, default=40)
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--label", default="direct-v4l2-current-controls")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.warmup < 0 or args.seconds <= args.warmup + 2:
        parser.error("Require nonnegative warmup and capture > warmup + 2 seconds")
    capture(args)
