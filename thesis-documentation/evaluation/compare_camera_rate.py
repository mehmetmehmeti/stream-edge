"""Temporarily test the dynamic-frame-rate control, then restore and remeasure."""
import argparse
import json
from pathlib import Path
import signal
import subprocess
from types import SimpleNamespace

from camera_diagnostic import capture, controls, settings


def interrupted(*_):
    raise KeyboardInterrupt


def compare(args):
    original = controls(settings(args.device)).get("exposure_dynamic_framerate")
    if original is None:
        raise RuntimeError("Camera does not report the dynamic-frame-rate control")
    if args.output.exists():
        raise RuntimeError("Choose a fresh output directory")
    args.output.mkdir(parents=True)
    common = {"device": args.device, "seconds": args.seconds, "warmup": 5}
    try:
        subprocess.run(["v4l2-ctl", "--device", args.device, "--set-ctrl", "exposure_dynamic_framerate=0"], check=True)
        capture(SimpleNamespace(**common, output=args.output / "dynamic-disabled",
                                label="direct-v4l2-dynamic-framerate-disabled"))
    finally:
        subprocess.run(["v4l2-ctl", "--device", args.device, "--set-ctrl",
                        f"exposure_dynamic_framerate={original}"], check=True)
        restored = controls(settings(args.device)).get("exposure_dynamic_framerate")
        (args.output / "restoration.json").write_text(json.dumps({"original": original, "restored": restored}) + "\n")
        if restored != original:
            raise RuntimeError("Camera control restoration failed")
        print(f"Camera dynamic-frame-rate control restored to {restored}", flush=True)
    capture(SimpleNamespace(**common, output=args.output / "restored",
                            label="direct-v4l2-original-control-restored"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="/dev/video0")
    parser.add_argument("--seconds", type=float, default=40)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 7:
        parser.error("Capture must exceed warmup plus two seconds")
    signal.signal(signal.SIGTERM, interrupted)
    compare(args)
