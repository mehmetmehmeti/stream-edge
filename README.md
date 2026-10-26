# Thesis Verification Evidence

Public evidence for professor comment 43 of Mehmet Mehmeti's thesis:
**Projektimi dhe vlerësimi i një arkitekture heterogjene për transmetim dhe regjistrim të optimizuar të videove në kohë reale**.

This branch contains only the four existing verification reports named in that comment, an integrity manifest, and an offline checker. It does not change the application's main branch. Preparing this publication did not rerun the camera tests or start the application services.

## Reports And Producers

| Report | What Was Recorded | Producer Reference |
| --- | --- | --- |
| [workflow-result.json](workflow-result.json) | Seven workflow assertions, generated stream/video IDs, and an FFprobe result: H.264, 640×480, 183 frames, 6.020667 seconds. | [verify-workflow.cjs](https://github.com/mehmetmehmeti/stream-edge/blob/843e99f9c93726ec99ba6c0b54ff430b992d814b/scripts/verify-workflow.cjs) |
| [recovery-result.json](recovery-result.json) | Three recovery assertions: pending command during disconnection, rejection of stale/duplicate replay, and recording/upload recovery. | [verify-recovery.cjs](https://github.com/mehmetmehmeti/stream-edge/blob/843e99f9c93726ec99ba6c0b54ff430b992d814b/scripts/verify-recovery.cjs) |
| [cloud-camera-result.json](cloud-camera-result.json) | Browser video dimensions, frame counters 11→611, pixel range 30, recording duration 3.936333 seconds, and an empty browser-error list. | [verify-cloud-camera.cjs](https://github.com/mehmetmehmeti/stream-edge/blob/843e99f9c93726ec99ba6c0b54ff430b992d814b/scripts/verify-cloud-camera.cjs) |
| [qt-camera.json](qt-camera.json) | Local live-preview observation: `live=true`, `renderedFrames=305`. | [Qt --verify-camera mode in main.cpp](https://github.com/mehmetmehmeti/frontend-edge/blob/e73de0eb6fb9b4cc674be4e0006b10ebdc389ff0/main.cpp#L173-L222) |

The source links pin the audited implementation used to explain the report producers. The historical JSON files do not embed a run timestamp, original executable hash, or source commit, so these links do not establish a cryptographic binding to the historical executions. The current Qt verification mode also emits a `fullscreen` field; that field is absent from the historical report and has not been added retrospectively.

### Production Method

- `verify-workflow.cjs` authenticates against the two application APIs, drives start/stop and recording commands, waits for synchronized completion, compares recording SHA-256 values, probes a recording with FFprobe, and checks authenticated HTTP Range playback before writing its report.
- `verify-recovery.cjs` deliberately stops and restarts the test backends, checks a pending command, injects a stale event and its duplicate, then verifies upload retry and converged recording metadata after reconnection.
- `verify-cloud-camera.cjs` uses Playwright/Chromium to read video playback counters and sample a 64×48 canvas. `pixelRange` is the red-channel maximum minus minimum; the checked script rejects values below 5. It also opens a recording and captures its dimensions and duration.
- The Qt `--verify-camera` mode observes the preview and writes its live flag and rendered-frame count to the path selected through `DIPLOMA_TEST_OUTPUT`.

The producer scripts require a configured test installation, camera access, a dedicated test account and running services. Credentials are supplied through the environment, not these reports. Recovery verification intentionally interrupts test services; it should not be run against a production deployment. [The camera runbook](https://github.com/mehmetmehmeti/stream-edge/blob/843e99f9c93726ec99ba6c0b54ff430b992d814b/docs/CAMERA-RUNBOOK.md) describes the application setup. No rerun was performed to publish this package.

## Interpretation Limits

These are historical functional-verification reports, separate from the later F450, two-laptop LAN, optical-latency and buffering A/B measurements discussed in the thesis. In particular:

- `first.time=1.03` is the browser video's `currentTime`, not measured startup latency.
- Counters 11→611 and 305 lack a recorded observation interval and cannot independently establish FPS.
- An empty browser-error list or a nonuniform image is not a comprehensive quality or loss metric.
- The recovery results support the exercised scenarios, not every possible failure or exactly-once physical execution.
- The hashes in this publication verify the report files, not the underlying MP4 recordings or historical test execution.

## Privacy And Integrity

The four report files were reviewed structurally and for credential/personal-data patterns. They contain test-result statements, generated test session/video UUIDs and numerical metadata; no passwords, JWTs, access tokens, private account email addresses, host paths or database connection strings were found. All four are published byte-for-byte unchanged. Sanitization here means selecting this reviewed allowlist, not editing measured results.

Authentication dumps, environment files, database dumps, operational logs, screenshots, MP4 files and other runtime files are excluded. Generated session IDs are identifiers, not authorization credentials. No claim of a complete raw-data release is made.

[manifest.json](manifest.json) records the SHA-256 and size of each report. Run the offline checks from this directory:

```bash
python3 verify-evidence.py
sha256sum -c SHA256SUMS.txt
```

The checker validates report schemas, expected historical values and file hashes. It does not rerun the application tests or contact any service.
