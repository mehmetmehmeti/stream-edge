# Two-Computer LAN Evaluation

Run from the project root with the edge services on this laptop and backend-web,
frontend-web, media-server, and diploma_web on the second computer.

Required edge configuration includes both MEDIA_SERVER_API_URL (readiness
checks) and MEDIA_SERVER_PUBLIC_URL. They are different settings. For this run,
the API readiness URL is http://192.168.3.162:3010.

```bash
node thesis-documentation/evaluation/lan-evaluation.cjs --run --mode short
node thesis-documentation/evaluation/lan-evaluation.cjs --run --mode cycles --cycles 30
```

Do not issue manual commands from either application during automation. The
runner refuses outstanding commands and active recordings. It stops an existing
live stream through the normal web API before starting its own test. It restarts
only backend-edge and Qt to enable optional transfer and Qt frame logging, using
the existing account identity and shared private JWT configuration locally. No
new account is registered and no credentials or playback tickets are printed.

The camera configuration is preserved: native 1280 x 704 YUYV, nominal 15 FPS.
The legacy C++ command executor must remain disabled. Only backend-edge executes
commands. Do not use run-evaluation.sh for this deployment: that older wrapper
starts a single-laptop stack and overrides addresses with loopback defaults.

## Protocol

- Short test: stream start, 10 seconds live-only observation, recording start,
  at least 20 seconds live-plus-recording observation, recording stop, upload
  convergence, MP4 verification, browser playback/pause/seek/fullscreen, stream stop.
- Cycles: 30 repetitions of stream start, first decoded browser frame,
  recording start, at least 5 seconds observation, recording stop, upload/file
  verification, and stream stop. This is 120 measured commands, not 30 commands.
- A cycle passes only after both replicas report completed commands with equal
  versions, live/ended stream states, and completed recording metadata.
- Hashes and decoded frame counts are compared between the actual local original
  and MP4 bytes downloaded from the server's authenticated playback endpoint.
  The server path from database metadata is never opened as a laptop path.
- A separate server-session hash/ffprobe report establishes independent direct
  server filesystem verification. A diagnostic browser on the edge laptop does
  not establish that playback controls were exercised on the second computer.
- Failed/aborted runs are retained in separate directories and excluded from
  successful-run percentiles. The runner aborts on the first failed cycle; it
  does not clear queues or silently retry a failed command as a passing trial.

## Timings

Durations use the initiating laptop's monotonic clock, not subtraction of clocks
from different hosts. Command time runs from POST initiation to the first
observation of COMPLETED on both replicas and includes replication and polling.
Startup time runs from the same POST initiation to the first observed decoded
frame in the diagnostic browser, including normal dashboard refresh behavior.
These are client-observed completion/startup measurements, not pure executor
runtime or optical camera-to-screen latency. Command polls pause 200 ms; frame
polls pause 100 ms; network/API/automation processing adds to those pauses.

The mean and median are computed from actual readings; p95 uses nearest rank,
sorted_values[ceil(0.95 * n) - 1]. Internal recorder finalization, hash preparation,
and successful upload-request time are separate measurements. Verification
download time is not upload time. Missing measurements remain null.

## Evidence

Each .runtime/lan-evaluation/<UTC run ID>/ directory contains result.json,
events.jsonl, Qt frame samples, local process resources, optional system-wide
NVIDIA samples, transfer timings, screenshots, and downloaded verification MP4s.
The result records service placement, local host/network information, Git commits,
stream/video/command IDs, replica versions, local synchronization events, actual
camera configuration, and the runner's SHA-256.

Additional server-side viewers are counted in sampled media health. Measurements
must not be described as a one-viewer experiment when another server browser was
connected. Local CPU/RSS/GPU samples describe this laptop only; collect server
resource evidence in the server session. RSS sums can double-count shared pages;
100% CPU means one logical core. GPU samples are system-wide, not proof of GPU
encoding.

Preserve reports and raw failed attempts. Review screenshots, MP4s, host details,
and private application logs before publication. Optical timer photographs, a
controlled repeated baseline comparison, and the >=10-minute recording remain
distinct experiments; completing 30 short cycles does not replace them.
