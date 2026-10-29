# LAN Evaluation Results: 22 October 2026

## Configuration

Edge host: mehmet-work-Cyborg-15-A12UC, 192.168.3.50. The web/backend/media endpoints were on the second laptop at 192.168.3.162.
Camera capture, recording and web export remained at native 1280 x 704 YUYV, nominal 15 FPS. The recorder used the existing CPU path. backend-edge remained the sole command executor.
The physical two-laptop photograph was collected separately by the user. Local host/process/listener evidence is retained in each run; independent server-side evidence is collected by the coordinating session.

## Short End-to-End Check

- Stream: `1e51016a-25ae-41e1-a032-7283501a203f`; recording: `7a3b41bc-9eaa-4a92-8e88-123e7a85b770`.
- All four commands completed on both replicas with matching versions; the stream ended normally.
- Local MP4 and server-served MP4: 16596082 bytes, 330 decoded frames, 21.97 seconds.
- SHA-256: `be67a58e479ffedb205d9e5edc44a45cc4c51c97d766908650af13a4a2556659`. Hash, decoded frame count, file size and replica recording metadata matched.
- Internal finalization: 208.13 ms. Successful upload request: 3066.65 ms; hash preparation: 27.01 ms.
- Authenticated remote playback, pause, seek, fullscreen and byte-range requests passed in the diagnostic browser on the edge laptop.
- Browser live-only observation: 10.01 s, 14.98 presented FPS. Live-plus-recording observation: 20.02 s, 14.94 presented FPS. This single functional check is not the repeated controlled baseline experiment.

## Thirty Measured Cycles

30/30 cycles passed, covering 120/120 completed commands and 30 recording uploads. Every cycle included stream start, first decoded browser frame, recording start, at least five seconds of observation, recording stop, file verification and stream stop.

| Client-observed metric | n | Mean (ms) | p50 (ms) | p95 (ms) |
|---|---:|---:|---:|---:|
| stream start | 30 | 3381.52 | 3521.70 | 4755.15 |
| recording start | 30 | 3011.95 | 3029.81 | 3949.89 |
| recording stop | 30 | 3007.32 | 2914.36 | 4773.20 |
| stream stop | 30 | 2573.93 | 2562.81 | 4000.45 |
| startup to first browser frame | 30 | 3486.43 | 3654.71 | 4798.96 |

Command timings run from client POST initiation to first observation of COMPLETED on BOTH replicas. Startup runs from POST initiation to first observed decoded browser frame, including normal dashboard refresh. Durations use the initiating laptop monotonic clock; command polls pause 200 ms and frame polls 100 ms, plus request/automation processing. These are not optical camera-to-screen delay measurements. p95 uses the nearest-rank method.

## Video And Files

- Browser recording intervals total 150.34 s; weighted presented FPS 13.71, decoded FPS 13.98.
- Playback drops: 41/2106 (1.95%). RTP packet-loss delta: 136. Recorder queue drops: 0. These are distinct counters.
- Weighted received video RTP bitrate: 2.46 Mbit/s.
- Qt per-cycle rendered FPS: mean 14.83, range 13.55 to 15.14 across 30 observed cycles.
- 30/30 file hashes and 30/30 decoded-frame comparisons matched; 3607 frames across the cycle recordings.
- Finalization mean/p95: 727.36/1066.89 ms. Successful upload-request mean/p95: 2345.50/4445.77 ms. Upload timing includes response/server storage/checksum work; verification downloads are separate.

## Evidence And Scope

Short check: `.runtime/lan-evaluation/2026-10-22T17-56-52-657Z`.
30 cycles: `.runtime/lan-evaluation/2026-10-22T17-58-29-117Z`.
result.json and events.jsonl retain command histories, replica statuses/versions, recording metadata, frame samples and service evidence. analysis.json and command-timings.csv contain derived results. transfers.jsonl records hash preparation/upload timing, qt.jsonl records actual Qt frame counters, and resources.jsonl contains local CPU/RSS and applicable system-wide NVIDIA samples.
Local resource summaries cover the whole automation run, including startup, upload, verification downloads and FFprobe. They are not server resource measurements or a steady-state recording-overhead comparison. Actual viewer counts are retained in media health samples; an extra server browser must not be described as one viewer.
The three earlier short-check harness failures are retained separately: one frame-counter initialization issue and two seek-control automation issues. They are excluded from this completed 30-cycle experiment. No application failure or command retry has been hidden as a passing cycle.
Original-versus-server-served bytes were verified over the LAN. Run verify-lan-on-server.cjs with expected-server-evidence.json on the second laptop for independent on-disk SHA-256/ffprobe and database evidence. Server-browser manual playback evidence remains distinct from this edge-laptop diagnostic browser.

## Cleanup

The laptop databases and original recording locations were selectively cleaned of 15 October test data after private database dumps and hash-verified file backups. Reports, users, devices, sequence counters, synchronization history and 22 October data were preserved. Local backup root: .runtime/oct15-cleanup/2026-10-22T17-48-30-863Z. The user confirmed that the server session also deleted the old data; its exact backup path/count report was not supplied to this session.
The 15 October reports remain valid historical evidence for those runs. The new short/cycle experiment does not replace the earlier >=10-minute recording, repeated baseline comparison, or the separate timer-photograph analysis.
