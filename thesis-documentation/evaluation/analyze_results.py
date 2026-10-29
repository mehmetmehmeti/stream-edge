"""Summarize measured intervals; never infer missing historical timings."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(value for value in values if value is not None and math.isfinite(value))
    if not values:
        return {"n": 0, "mean": None, "p50": None, "p95": None, "min": None, "max": None}
    return {"n": len(values), "mean": statistics.mean(values), "p50": statistics.median(values),
            "p95": values[math.ceil(0.95 * len(values)) - 1], "min": values[0], "max": values[-1]}


def lines(file):
    return [json.loads(line) for line in file.read_text().splitlines() if line.strip()] if file.exists() else []


def qt_metrics(samples, start, end):
    selected = [sample for sample in samples if start <= sample["utc_ms"] <= end and sample.get("preview_present")]
    if len(selected) < 2:
        return {"status": "missing timed Qt samples", "fps": None}
    first, last = selected[0], selected[-1]
    seconds = (last["elapsed_ns"] - first["elapsed_ns"]) / 1e9
    frames = last["rendered_frames"] - first["rendered_frames"]
    if seconds <= 0 or frames < 0:
        return {"status": "invalid interval/counter reset", "fps": None}
    return {"status": "measured", "interval_s": seconds, "rendered_frames": frames, "fps": frames / seconds,
            "bridge_dropped_frames": last["bridge_dropped_frames"] - first["bridge_dropped_frames"],
            "invalid_frames": last["invalid_frames"] - first["invalid_frames"],
            "interop_failures": last["interop_failures"] - first["interop_failures"],
            "notes": ["Missing-frame polls are not lost video frames.",
                      "Bridge counters can be zero when the preview uses the GStreamer SHM path.",
                      "Without a source-frame denominator, no total camera-to-Qt loss percentage is inferred."]}


def analyze(directory):
    result = json.loads((directory / "benchmark-result.json").read_text())
    resources = lines(directory / "resources.jsonl")
    qt = lines(directory / "qt.jsonl")
    rows = []
    for condition in result["conditions"]:
        start, end = condition["utc_start_ms"], condition["utc_end_ms"]
        selected = [sample for sample in resources if sample.get("valid_cpu_interval")
                    and start <= sample["utc_ms"] <= end
                    and sample["utc_ms"] - sample["interval_s"] * 1000 >= start]
        components = sorted({key for sample in selected for key in sample["components"]})
        usage = {name: {
            "cpu_percent": stats([sample["components"].get(name, {}).get("cpu_percent") for sample in selected]),
            "rss_mib": stats([sample["components"][name]["rss_bytes"] / 1024**2 for sample in selected if name in sample["components"]])
        } for name in components}
        totals_cpu = [sum(component["cpu_percent"] for component in sample["components"].values()) for sample in selected]
        totals_ram = [sum(component["rss_bytes"] for component in sample["components"].values()) / 1024**2 for sample in selected]
        gpu_usage, gpu_memory = [], []
        for sample in selected:
            if not sample.get("gpu_csv"):
                continue
            for gpu_row in csv.reader(sample["gpu_csv"].splitlines()):
                try:
                    gpu_usage.append(float(gpu_row[0]))
                    gpu_memory.append(float(gpu_row[1]))
                except (ValueError, IndexError):
                    continue
        row = {**condition, "qt": qt_metrics(qt, start, end), "resources": usage,
               "total_cpu_percent": stats(totals_cpu), "total_rss_mib": stats(totals_ram),
               "system_gpu_percent": stats(gpu_usage), "system_gpu_memory_mib": stats(gpu_memory)}
        rows.append(row)
    groups = {}
    for recording in (False, True):
        selected = [row for row in rows if row["recording"] == recording and row["name"] != "long-recording"]
        groups["live_record" if recording else "live_only"] = {
            "repeats": len(selected),
            "web_presented_fps": stats([row["presented_fps"] for row in selected]),
            "qt_rendered_fps": stats([row["qt"]["fps"] for row in selected]),
            "received_rtp_mbps": stats([row["received_rtp_mbps"] for row in selected]),
            "total_cpu_percent": stats([row["total_cpu_percent"]["mean"] for row in selected]),
            "total_rss_mib": stats([row["total_rss_mib"]["mean"] for row in selected]),
            "system_gpu_percent": stats([row["system_gpu_percent"]["mean"] for row in selected]),
            "system_gpu_memory_mib": stats([row["system_gpu_memory_mib"]["mean"] for row in selected]),
        }
    comparisons = {}
    for metric in ("web_presented_fps", "qt_rendered_fps", "received_rtp_mbps", "total_cpu_percent", "total_rss_mib", "system_gpu_percent", "system_gpu_memory_mib"):
        baseline, recording = groups["live_only"][metric]["mean"], groups["live_record"][metric]["mean"]
        comparisons[metric] = {"record_minus_baseline": recording - baseline if baseline is not None and recording is not None else None,
                               "percent_change": (recording - baseline) / baseline * 100 if baseline and recording is not None else None}
    summary = {"status": result["status"], "run_id": result["run_id"], "conditions": rows, "groups": groups,
               "comparisons": comparisons, "recordings": result["recordings"],
               "unmeasured": ["Optical latency", "30-cycle command/startup evaluation", "Distributed two-computer deployment"],
               "interpretation": ["This comparison reports observed differences with parallel recording, not proof that recording improves FPS or that every optimization improves performance.",
                                  "Group statistics summarize replicate means; p95 of three replicates is not latency p95.",
                                  "CPU uses one-core=100%; summed RSS is not unique physical memory."]}
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    fields = ["condition", "interval_s", "web_presented_fps", "qt_rendered_fps", "received_rtp_mbps", "cpu_percent", "rss_mib", "system_gpu_percent", "system_gpu_memory_mib"]
    with (directory / "comparison.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields); writer.writeheader()
        for row in rows:
            writer.writerow(dict(zip(fields, [row["name"], row["interval_s"], row["presented_fps"], row["qt"]["fps"],
                row["received_rtp_mbps"], row["total_cpu_percent"]["mean"], row["total_rss_mib"]["mean"],
                row["system_gpu_percent"]["mean"], row["system_gpu_memory_mib"]["mean"]])))
    table = ["# Measured comparison", "", "| Condition | Web FPS | Qt FPS | RTP Mbit/s | CPU % | RSS MiB |",
             "|---|---:|---:|---:|---:|---:|"]
    fmt = lambda value: "not measured" if value is None else f"{value:.3f}"
    for row in rows:
        values = [row["presented_fps"], row["qt"]["fps"], row["received_rtp_mbps"], row["total_cpu_percent"]["mean"], row["total_rss_mib"]["mean"]]
        table.append("| " + row["name"] + " | " + " | ".join(map(fmt, values)) + " |")
    table.extend(["", *summary["interpretation"], "", "Not measured here: " + ", ".join(summary["unmeasured"])])
    (directory / "results.md").write_text("\n".join(table) + "\n")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    result = analyze(args.directory)
    print(f"Summary and CSV saved to {args.directory}; run status: {result['status']}")
