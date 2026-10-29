"""Collect specifications on the actual machine, without secrets or camera capture."""
import argparse
import datetime
import json
import os
from pathlib import Path
import platform
import re
import subprocess

REPOS = ("backend-edge", "backend-web", "frontend-edge", "frontend-web", "media-server", "stream-edge")


def command(args):
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=20)
        return {"returncode": result.returncode, "stdout": result.stdout.strip(), "stderr": result.stderr.strip()}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"unavailable": str(exc)}


def collect(root):
    versions = {}
    for repo in REPOS:
        package = root / repo / "package.json"
        if package.exists():
            declared = json.loads(package.read_text())
            actual = {}
            for name in ("next", "react", "@nestjs/core", "@prisma/client", "mediasoup", "mediasoup-client", "typescript"):
                installed = root / repo / "node_modules" / name / "package.json"
                if installed.exists():
                    actual[name] = json.loads(installed.read_text()).get("version")
            versions[repo] = {"package_version": declared.get("version"), "installed": actual}
    memory = {}
    for line in Path("/proc/meminfo").read_text().splitlines():
        key, value = line.split(":", 1)
        if key in ("MemTotal", "MemAvailable"):
            memory[key] = value.strip()
    git = {}
    for repo in REPOS:
        base = ["git", "-C", str(root / repo)]
        git[repo] = {"commit": command(base + ["rev-parse", "HEAD"]),
                     "status": command(base + ["status", "--short"]),
                     "diff_sha256": command(["bash", "-c", 'git -C "$1" diff HEAD | sha256sum', "bash", str(root / repo)])}
    configs = {}
    allowed = {"STREAMING_CAMERA_WIDTH", "STREAMING_CAMERA_HEIGHT", "STREAMING_CAMERA_FPS", "STREAMING_RECORD_FPS",
               "STREAMING_CAMERA_FRAME_RATE", "STREAMING_CAMERA_PIXEL_FORMAT", "STREAMING_CAMERA_DYNAMIC_FRAME_RATE",
               "STREAMING_CAMERA_EXPOSURE_TIME", "STREAMING_CAMERA_GAIN", "STREAMING_RECORD_WIDTH", "STREAMING_RECORD_HEIGHT",
               "STREAMING_RECORD_MODE", "STREAMING_RECORD_BITRATE_KBPS", "MEDIA_PUBLISH_VIDEO_CODEC",
               "STREAMING_CAMERA_DEVICE", "STREAMING_SOURCE_TYPE", "STREAMING_FRAMEBRIDGE_ENABLED"}
    for line in (root / "stream-edge/streaming-service/.env").read_text().splitlines():
        key, separator, value = line.partition("=")
        if separator and key.strip() in allowed:
            configs[key.strip()] = os.environ.get(key.strip(), value.strip().strip("\"'"))
    camera_device = configs.get("STREAMING_CAMERA_DEVICE", "/dev/video0")
    cache_versions = {}
    resolved_versions = {}
    for name, cache in (("qt", root / "frontend-edge/build-local/CMakeCache.txt"),
                        ("stream", root / "stream-edge/streaming-service/build/CMakeCache.txt")):
        if cache.exists():
            selected = [line for line in cache.read_text().splitlines()
                        if line.startswith(("Qt6_DIR:", "holoscan_DIR:", "CUDAToolkit_", "CUDA_TOOLKIT_", "GSTREAMER_VERSION:", "CMAKE_CXX_COMPILER:"))]
            cache_versions[name] = selected
            paths = dict(line.split("=", 1) for line in cache.read_text().splitlines()
                         if line.startswith(("Qt6_DIR:", "holoscan_DIR:", "CUDAToolkit_NVCC_EXECUTABLE:")))
            for prefix, filename in (("Qt6_DIR:", "Qt6ConfigVersion.cmake"), ("holoscan_DIR:", "holoscan-config-version.cmake")):
                package_path = next((value for key, value in paths.items() if key.startswith(prefix)), None)
                if package_path:
                    candidates = [Path(package_path) / filename]
                    candidates += list(Path(package_path).glob("*ConfigVersion.cmake"))
                    candidates += list(Path(package_path).glob("*ConfigVersionImpl.cmake"))
                    candidates += list(Path(package_path).glob("*-config-version.cmake"))
                    for candidate in candidates:
                        if not candidate.exists():
                            continue
                        match = re.search(r'set\(PACKAGE_VERSION\s+"?([0-9.]+)', candidate.read_text())
                        if match:
                            resolved_versions[prefix.rstrip(":").replace("_DIR", "")] = match.group(1)
                            break
            compiler = next((value for key, value in paths.items() if key.startswith("CUDAToolkit_NVCC_EXECUTABLE:")), None)
            if compiler:
                resolved_versions["cuda_compiler"] = command([compiler, "--version"])
    return {
        "collected_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "hostname": platform.node(),
        "kernel": platform.release(), "os_release": Path("/etc/os-release").read_text(),
        "cpu": command(["lscpu", "--json"]), "logical_cpu_count": os.cpu_count(), "memory": memory,
        "gpu": command(["nvidia-smi", "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"]),
        "display_devices": command(["lspci"]),
        "camera_capabilities": command(["v4l2-ctl", "--device", camera_device, "--list-formats-ext"]),
        "camera_settings": command(["v4l2-ctl", "--device", camera_device, "--all"]),
        "software": {name: command(args) for name, args in {
            "node": ["node", "--version"], "npm": ["npm", "--version"], "ffprobe": ["ffprobe", "-version"],
            "gstreamer": ["gst-launch-1.0", "--version"], "qt": ["qmake6", "-query", "QT_VERSION"],
            "cuda_compiler": ["nvcc", "--version"], "postgresql": ["psql", "--version"],
            "chrome": ["google-chrome", "--version"], "cmake": ["cmake", "--version"]}.items()},
        "packages": versions, "cmake_cache": cache_versions, "resolved_versions": resolved_versions,
        "repositories": git, "capture_configuration": configs,
        "notes": ["Package versions are installed versions, not package.json ranges.",
                  "Camera capability query does not start a capture session.",
                  "GPU presence does not establish GPU encoding or GPU use in a specific branch."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(collect(args.root), indent=2) + "\n")
    print(f"Specifications saved to {args.out}")
