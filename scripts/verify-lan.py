#!/usr/bin/env python3
"""Portable saved-evidence verification only; no service/database/camera/network access."""
import argparse
import copy
import datetime
import hashlib
import json
import math
import re
from pathlib import Path


def load(root, path):
    raw = (root / path).read_text()
    return [json.loads(s) for s in raw.splitlines() if s.strip()] if path.endswith(".jsonl") else json.loads(raw)


def check(ok, message):
    if not ok:
        raise AssertionError(message)


def same(a, b, label):
    if isinstance(a, dict):
        check(isinstance(b, dict) and set(a) == set(b), label + ": keys")
        for k in a:
            same(a[k], b[k], label + "." + k)
    elif isinstance(a, (int, float)) and not isinstance(a, bool) and isinstance(b, (int, float)):
        check(math.isclose(a, b, rel_tol=1e-11, abs_tol=1e-8), label)
    else:
        check(a == b, label)


def summary(values):
    x = sorted(v for v in values if isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v))
    n = len(x)
    if not n:
        return dict(n=0, mean=None, p50=None, p95=None, min=None, max=None)
    return dict(n=n, mean=sum(x)/n, p50=x[n//2] if n % 2 else (x[n//2-1]+x[n//2])/2,
                p95=x[math.ceil(.95*n)-1], min=x[0], max=x[-1])


def group(route):
    if route == "/streams/start":
        return "stream_start"
    parts = route.split("/")
    if len(parts) == 5 and parts[1] == "streams" and parts[3] == "recordings":
        return "recording_" + parts[4]
    check(len(parts) == 4 and parts[1] == "streams" and parts[3] == "stop", "unknown command route")
    return "stream_stop"


def aggregate(run, resources):
    good = [c for c in run["cycles"] if c["status"] == "passed"]
    metrics = [c["live_recording"]["metrics"] for c in good]
    seconds = sum(x["interval_s"] for x in metrics)
    def total(k):
        return sum(x[k] for x in metrics)
    out = dict(requested_cycles=run["requested_cycles"], passed_cycles=len(good),
        failed_cycles=sum(c["status"] == "failed" for c in run["cycles"]), measured_commands=len(run["commands"]),
        command_ms={k: summary([c["observed_both_completed_ms"] for c in run["commands"] if group(c["route"]) == k])
                    for k in ("stream_start", "recording_start", "recording_stop", "stream_stop")},
        startup_ms=summary([c["startup"]["observed_startup_ms"] for c in good]),
        browser_recording_observations=dict(total_interval_s=seconds,
            presented_fps_weighted=total("presented_frames")/seconds,
            decoded_fps_weighted=total("decoded_frames")/seconds,
            playback_dropped_frames=total("playback_dropped_frames"), playback_total_frames=total("playback_total_frames"),
            playback_drop_percent=total("playback_dropped_frames")/total("playback_total_frames")*100,
            packets_lost_delta=total("packets_lost_delta"),
            received_rtp_mbps_weighted=sum(x["received_rtp_mbps"]*x["interval_s"] for x in metrics)/seconds),
        qt_per_cycle_fps=summary([c["qt"]["rendered_fps"] for c in good]),
        finalization_ms=summary([c["recorder_finalization"]["finalization_duration_ms"] for c in good]),
        upload_request_ms=summary([c["file"]["upload_timing"]["request_duration_ms"] for c in good]),
        recorder_queue_drops=sum(c["recorder_finalization"]["dropped_frames"] for c in good),
        hash_matches=sum(c["file"]["hashes_match"] for c in good),
        decoded_frame_matches=sum(c["file"]["decoded_frames_match"] for c in good),
        total_recorded_frames=sum(int(c["file"]["local_probe"]["streams"][0]["nb_read_frames"]) for c in good))
    components = {}
    for name in ("stream-edge", "backend-edge", "frontend-edge", "browser"):
        components[name] = dict(
            cpu_percent_one_core=summary([s["components"][name]["cpu_percent"] for s in resources if s["valid_cpu_interval"]]),
            rss_mib=summary([s["components"][name]["rss_bytes"]/1024**2 for s in resources]))
    out["resource_components"] = components
    return out


def data_checks(root, data):
    manifest = data["data/lan/expected-server-evidence.json"]
    original = data["data/server/supplied-verifier-original-report.json"]
    supplied = data["data/server/server-lan-verification.json"]
    db = data["data/server/selected-database-evidence.json"]
    all_commands, all_cycles = [], []
    for name, n in (("cycles", 30), ("short", 1)):
        prefix = "data/lan/" + name + "/"
        run, saved = data[prefix+"result.json"], data[prefix+"analysis.json"]
        check(run["status"] == "passed" and len(run["cycles"]) == n, name + " cycles/status")
        check(len(run["commands"]) == 4*n, name + " command count")
        got = aggregate(run, data[prefix+"resources.jsonl"])
        for k, v in got.items():
            same(v, saved["local_resources"]["components"] if k == "resource_components" else saved[k], name+"."+k)
        transfers = data[prefix+"transfers.jsonl"]
        qt = data[prefix+"qt.jsonl"]
        for c in run["cycles"]:
            check(c["status"] == "passed", "non-passing accepted cycle")
            check(c["file"]["sha256_original"] == c["file"]["sha256_remote_served"], "LAN file hash")
            check(c["file"]["range_request_status"] == 206, "authenticated range status")
            same(c["qt"]["rendered_frames"]/c["qt"]["interval_s"], c["qt"]["rendered_fps"], "Qt cycle FPS")
            start = datetime.datetime.fromisoformat(c["started_at"].replace("Z","+00:00")).timestamp()*1000
            end = datetime.datetime.fromisoformat(c["finished_at"].replace("Z","+00:00")).timestamp()*1000
            selected = [s for s in qt if start <= s["utc_ms"] <= end and s["live"]]
            check(len(selected) >= 2, "Qt samples missing")
            same((selected[-1]["elapsed_ns"]-selected[0]["elapsed_ns"])/1e9,c["qt"]["interval_s"],"Qt elapsed samples")
            check(selected[-1]["rendered_frames"]-selected[0]["rendered_frames"] == c["qt"]["rendered_frames"],"Qt rendered samples")
            check(any(t == c["file"]["upload_timing"] for t in transfers), "upload timing absent from transfer samples")
            for side in ("edge", "web"):
                check(c["ended_replicas"][side]["status"].lower() == "ended", "stream not ended")
        for cmd in run["commands"]:
            w, e = cmd["replicas"]["web"], cmd["replicas"]["edge"]
            check(w["status"].lower() == e["status"].lower() == "completed", "command replica status")
            check(w["version"] == e["version"], "command replica version")
        all_commands.extend(run["commands"])
        all_cycles.extend(run["cycles"])
    cycle_run = data["data/lan/cycles/result.json"]
    check(all(v == 2 for c in cycle_run["cycles"] for v in c["live_recording"]["additional_viewers"]), "viewer count")
    check(len(manifest["videos"]) == len(original["files"]) == 31, "31 selected files")
    check(len(manifest["commands"]) == len(original["commands"]) == len(all_commands) == 124, "124 commands")
    check(len(manifest["stream_ids"]) == 31 and len(original["streams"]) == 31, "31 streams")
    check(len(db["all_video_ids"]) == 33, "33 total videos")
    check({v["id"] for v in manifest["videos"]} == {c["video_id"] for c in all_cycles}, "video acceptance set")
    check({v["id"] for v in manifest["commands"]} == {c["id"] for c in all_commands}, "command acceptance set")
    byvideo = {v["id"]: v for v in db["videos"]}
    expected_videos = {v["id"]:v for v in manifest["videos"]}
    expected_commands = {v["id"]:v for v in manifest["commands"]}
    for c in all_cycles:
        expected = expected_videos[c["video_id"]]
        check(c["file"]["sha256_original"] == expected["sha256"], "manifest/LAN hash join")
        check(int(c["file"]["local_probe"]["streams"][0]["nb_read_frames"]) == int(expected["frame_count"]), "manifest/LAN frame join")
        check(int(c["file"]["local_probe"]["format"]["size"]) == int(expected["size_bytes"]), "manifest/LAN size join")
    for c in all_commands:
        check(c["replicas"]["web"]["version"] == expected_commands[c["id"]]["version"],"manifest/LAN command version")
    versions = []
    for report in (original, supplied):
        check(report["server_browser_playback_confirmed"] is None, "unobserved server playback")
        check(report["database_version"].find("16.15") >= 0, "application database version")
        for expected in manifest["videos"]:
            f = next(f for f in report["files"] if f["id"] == expected["id"])
            check(f["sha256"] == expected["sha256"], "server file hash")
            check(int(f["size_bytes"]) == int(expected["size_bytes"]), "server file size")
            check(int(f["probe"]["streams"][0]["nb_read_frames"]) == int(expected["frame_count"]), "server frame count")
            check(f["probe"]["streams"][0]["width"] == 1280 and f["probe"]["streams"][0]["height"] == 704, "resolution")
            same(float(f["probe"]["format"]["duration"]), float(expected["duration_seconds"]), "MP4 duration")
        for expected in manifest["commands"]:
            c = next(c for c in report["commands"] if c["id"] == expected["id"])
            check(c["status"] == "COMPLETED" and c["version"] == expected["version"], "server command version")
        check(all(s["status"] == "ENDED" for s in report["streams"]), "server stream ended")
    for expected in manifest["videos"]:
        actual = byvideo[expected["id"]]
        if actual["version"] != expected["version"]:
            check((expected["version"], actual["version"]) == (5, 6), "unexpected version evolution")
            events = cycle_run["local_sync_events"]["sync_inbox"]
            event_ids = {"d4127b7b-d029-43f6-9d78-bde948acd876":"9253a0b9-60af-423a-9174-5d12125d626f",
                         "06c662ff-3e50-4890-aa78-8524844cfd1b":"8976d075-3211-44fa-a750-98acfc9d7586"}
            check(any(e["entity_id"] == expected["id"] and e["version"] == 6 and
                      e["status"] == "APPLIED" and e["entity_status"] == "completed" and
                      e["event_id"] == event_ids.get(expected["id"]) for e in events), "missing applied event")
            versions.append(expected["id"])
    expected_versions = {"d4127b7b-d029-43f6-9d78-bde948acd876", "06c662ff-3e50-4890-aa78-8524844cfd1b"}
    check(set(versions) == expected_versions, "exactly two version exceptions")
    failures = supplied["supplemental_checks"]["failures"]
    check(set(failures) == {v+":version_matches_manifest" for v in expected_versions}, "two failed checks")
    check({k for k,v in supplied["supplemental_checks"]["checks"].items() if not v} == set(failures), "hidden failure")
    check(original["status"] == supplied["provided_verifier_status"] == "passed" and
          supplied["status"] == supplied["supplemental_checks"]["status"] == "failed", "failed status preserved")
    check(sum(int(v["frame_count"]) for v in manifest["videos"]) == 3937, "3937 frames")
    check(len(data["data/lan/excluded-attempts.json"]["attempts"]) == 3, "failed attempts retained")
    check(all(x["status"] == "failed" for x in data["data/lan/excluded-attempts.json"]["attempts"]), "failed attempts status")
    check(len(data["data/lan/short/summary-normalization.json"]["changes"]) == 5, "normalization ledger")
    changes = {x["pointer"]:x for x in data["data/lan/short/summary-normalization.json"]["changes"]}
    for key, before, after in (("n",2,1),("mean",1740.5539135,2148.18477),
                               ("p50",1740.5539135,2148.18477),("min",1332.923057,2148.18477)):
        row = changes["/summary/commands/~1stop/"+key]
        same(row["before"],before,"normalization before")
        same(row["after"],after,"normalization after")
    check("/summary_correction" in changes,"normalization annotation")
    check(db["selected_sync"]["sync_inbox"] == [{"status":"APPLIED","count":496}], "selected inbox")
    check(db["selected_sync"]["sync_outbox"] == [{"status":"APPLIED","count":281}], "selected outbox")
    check(any(x["status"] == "IGNORED" and int(x.get("count",x.get("n",0))) == 23
              for x in original["sync_counts"]["sync_inbox"]), "global ignored")
    backup = data["data/server/cleanup.json"]["backups"]["recordings"]
    check(backup["file_count"] == 1 and backup["missing_reference_count"] == 16, "backup limits")
    check(data["data/server/hardware.json"]["host"] == "SERVER_HOST" and
          data["data/server/hardware.json"]["capture_timestamp"] is None, "hardware alias/time")
    return {"status":"passed","cycles":30,"selected_files":31,"commands":124,"decoded_frames":3937,
            "expected_version_failures":2,"preparation_failures_preserved":3}


def validate_schema(value, spec):
    kind = spec["type"]
    if kind == "object":
        return isinstance(value, dict) and set(value) == set(spec["properties"]) and all(
            validate_schema(value[k],s) for k,s in spec["properties"].items())
    if kind == "array":
        return isinstance(value,list) and all(any(validate_schema(v,s) for s in spec["variants"]) for v in value)
    return (value is None if kind == "null" else isinstance(value,bool) if kind == "boolean" else
            isinstance(value,(int,float)) and not isinstance(value,bool) if kind == "number" else isinstance(value,str))


def verify(root, selftest=False):
    provenance = load(root, "provenance/lan-server.json")
    schemas = load(root, "provenance/schemas.json")
    data = {}
    for f in provenance["outputs"]:
        path = f["published_path"]
        check(not Path(path).is_absolute() and ".." not in Path(path).parts, "unsafe published path")
        raw = (root/path).read_bytes()
        check(hashlib.sha256(raw).hexdigest() == f["published_sha256"] and len(raw) == f["published_size_bytes"], "hash: "+path)
        if path.endswith((".json",".jsonl")) and path != "provenance/schemas.json":
            value = load(root,path)
            check(validate_schema(value, schemas["outputs"][path]["schema"]), "schema: "+path)
            data[path] = value
    def privacy(value):
        if isinstance(value,dict):
            forbidden = {"account_id","user_id","device_id","password","token","authorization","cookie",
                         "interfaces","processes","process_owner","claimed_by","playback_url","cloud_path","edge_local_path"}
            check(not (set(value) & forbidden),"private field")
            for v in value.values(): privacy(v)
        elif isinstance(value,list):
            for v in value: privacy(v)
        elif isinstance(value,str):
            check("/home/" not in value and not re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b",value),"private path/address")
            check(not re.search(r"(?i)(?:bearer\s+|postgres(?:ql)?://|[?&](?:token|ticket|signature)=)",value),"credential-shaped value")
    for path,value in data.items():
        if path.startswith("data/"): privacy(value)
    result = data_checks(root, data)
    if selftest:
        mutations = [
            ("hash", lambda d: d["data/server/supplied-verifier-original-report.json"]["files"][0].update(sha256="0"*64)),
            ("command", lambda d: d["data/server/supplied-verifier-original-report.json"]["commands"][0].update(version=-1)),
            ("status", lambda d: d["data/server/server-lan-verification.json"].update(status="passed")),
            ("cycle", lambda d: d["data/lan/cycles/result.json"]["cycles"].pop()),
            ("version", lambda d: d["data/server/selected-database-evidence.json"]["videos"][0].update(version=999))]
        for name, mutate in mutations:
            altered = copy.deepcopy(data)
            mutate(altered)
            try:
                data_checks(root, altered)
            except (AssertionError, KeyError, IndexError):
                continue
            raise AssertionError("self-test failed to reject "+name)
        injected = copy.deepcopy(data["data/lan/cycles/result.json"])
        injected["cycles"][0]["unexpected_private_payload"] = {"user_id":"NOT_A_REAL_ID"}
        check(not validate_schema(injected, schemas["outputs"]["data/lan/cycles/result.json"]["schema"]),
              "unknown-field self-test")
        result["negative_selftests"] = len(mutations)+1
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    print(json.dumps(verify(args.root,args.self_test),sort_keys=True))
