"""Sample thesis process CPU/RSS from /proc. CPU=100% means one logical core."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

REPOS = {"backend-edge", "backend-web", "frontend-edge", "frontend-web", "media-server", "stream-edge"}
running = True


def stop(*_):
    global running
    running = False


def processes(root, browser_pid):
    entries = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            raw = (entry / "stat").read_text()
            tail = raw[raw.rfind(")") + 2:].split()
            cwd = (entry / "cwd").resolve()
            relative = cwd.relative_to(root).parts
            component = relative[0] if relative and relative[0] in REPOS else None
        except (OSError, ValueError):
            try:
                raw = (entry / "stat").read_text()
                tail = raw[raw.rfind(")") + 2:].split()
            except OSError:
                continue
            component = None
        entries.append({"pid": int(entry.name), "ppid": int(tail[1]), "start_ticks": int(tail[19]),
                        "cpu_ticks": int(tail[11]) + int(tail[12]), "rss_pages": int(tail[21]), "component": component})
    browser = {browser_pid} if browser_pid else set()
    changed = True
    while changed:
        before = len(browser)
        browser.update(value["pid"] for value in entries if value["ppid"] in browser)
        changed = len(browser) != before
    for value in entries:
        if value["pid"] in browser:
            value["component"] = "browser"
    return [value for value in entries if value["component"] and value["pid"] != os.getpid()]


def sample(root, out, browser_pid, interval):
    ticks = os.sysconf("SC_CLK_TCK")
    page_size = os.sysconf("SC_PAGE_SIZE")
    previous = {}
    previous_time = time.monotonic()
    with out.open("w") as stream:
        while running:
            start = time.monotonic()
            elapsed = start - previous_time
            entries = processes(root, browser_pid)
            totals = {}
            details = []
            for entry in entries:
                key = (entry["pid"], entry["start_ticks"])
                old = previous.get(key)
                cpu = (entry["cpu_ticks"] - old) / ticks / elapsed * 100 if old is not None and elapsed > 0 else None
                rss = entry["rss_pages"] * page_size
                aggregate = totals.setdefault(entry["component"], {"cpu_percent": 0.0, "rss_bytes": 0, "process_count": 0})
                if cpu is not None:
                    aggregate["cpu_percent"] += cpu
                aggregate["rss_bytes"] += rss
                aggregate["process_count"] += 1
                details.append({**entry, "cpu_percent": cpu, "rss_bytes": rss})
            try:
                gpu_result = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,memory.used", "--format=csv,noheader,nounits"],
                                            capture_output=True, text=True, timeout=3)
                gpu = gpu_result.stdout.strip() if gpu_result.returncode == 0 else None
            except (OSError, subprocess.TimeoutExpired):
                gpu = None
            stream.write(json.dumps({"utc_ms": time.time() * 1000, "monotonic_s": start, "valid_cpu_interval": bool(previous),
                                     "interval_s": elapsed, "components": totals, "processes": details, "gpu_csv": gpu}) + "\n")
            stream.flush()
            previous = {(entry["pid"], entry["start_ticks"]): entry["cpu_ticks"] for entry in entries}
            previous_time = start
            time.sleep(max(0, interval - (time.monotonic() - start)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--browser-pid", type=int, default=0)
    parser.add_argument("--interval", type=float, default=1)
    args = parser.parse_args()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, stop)
    sample(args.root.resolve(), args.out, args.browser_pid, args.interval)
