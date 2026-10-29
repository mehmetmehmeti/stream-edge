# Pre-Encoder Buffering A/B Protocol

## Purpose And Scope

Evaluate one implemented low-latency mechanism for professor comment 18(b): the pre-encoder live queue in the existing F450 shared-memory to WHIP/mediasoup/WebRTC pipeline. This is an ablation of that queue policy, not a claim that all buffering anywhere in the baseline architecture is unlimited. The one-slot FrameBridge, appsrc and upstream shared-memory publisher remain unchanged.

The experiment runs entirely on the edge laptop to hold the hardware and network topology fixed. It includes the actual web application and WebRTC receiver as well as Qt preview. It does not infer web behaviour from Qt, and it does not assert equal end-to-end latency across local and two-laptop deployments. Earlier two-laptop results remain separate evidence.

## Fixed Conditions

- Intel F450, 1280 x 704 YUYV, nominal 15 FPS; no exposure/gain/control changes.
- Camera source and recorder/export rates remain aligned at 15 FPS.
- One Qt view and one browser viewer; live VP8 encoder deadline=1, cpu-used=8, target bitrate 2,500,000 bit/s, keyframe interval 15; RTP/WHIP unchanged.
- CPU recording enabled throughout warmup and sampling in every trial, with existing H.264 recorder configuration. The experiment does not claim hardware encoding.
- Original 16 MiB shared-memory pool and all upstream latest-frame/bounded policies remain fixed.
- Native instrumentation is identical in both arms. It adds monotonic timestamp pad probes to a separately compiled copy of the existing WHIP client; the production binary is not replaced.
- Profiles run sequentially, never as two competing camera streams.

## Treatment And Loads

Only the named pre-encoder queue policy differs:

| Profile | max-size-buffers | max-size-bytes | max-size-time | leaky |
|---|---:|---:|---:|---|
| FIFO baseline | 0 | 0 | 0 | no |
| Latest-frame | 2 | 0 | 0 | downstream (discard old frames) |

Limits of zero disable the corresponding limits of this queue, not upstream limits. The 16 MiB shared-memory pool can impose backpressure and cap practical queue occupancy. Therefore unbounded RSS growth is not an assumed outcome.

Both profiles contain the same identity stage immediately after the queue. Its sleep-time is 0 microseconds for normal trials and 100,000 microseconds per raw frame for controlled-slowdown trials. This is a deliberately induced bottleneck, not a claim of naturally occurring deployment slowdown. Conversion and encoding time add to this sleep; output is not assumed to be exactly 10 FPS.

## Schedule

Three pairs under each load, each trial with 15 s warmup and 60 s measurement. Normal order: FIFO/latest, latest/FIFO, FIFO/latest. Slow order: latest/FIFO, FIFO/latest, latest/FIFO. A fresh stream/publisher, empty queue and separate recording are created per trial. Total planned warmup plus measurement is 15 minutes; command routing, startup, finalization and verification add time.

A separate four-trial camera pilot uses 3 s warmup and 6 s measurement. A separate synthetic videotestsrc check verifies instrumentation, PTS correspondence and frame-discard accounting. Neither is pooled into the repeated camera experiment. All unsuccessful attempts remain evidence, clearly identified.

## Measurements

Primary metric: queue residence time, from sink-pad entry to source-pad output, in the publisher process's GLib monotonic microseconds. Report each trial's median and nearest-rank p95, then summarize those per-trial statistics across three trials. Individual frames are not treated as independent experiment repetitions.

Secondary publisher slice: entry at that queue to encoded VP8 output. It includes queue waiting, induced delay, conversion and encoding. It excludes camera capture, earlier bridge/SHM waiting, transport, receiver buffering and display. Neither metric is optical end-to-end latency. Earlier 160/205 ms photo estimates are not reused as A/B measurements.

Frame matching uses the unchanged buffer PTS within the publisher process; unmatched or reordered output invalidates the measurement. Native SIGUSR1/SIGUSR2 markers delimit the monotonic measurement window. Outputs occurring inside that window form the latency cohort, including frames that entered shortly before its start. Pending tail and startup are not classified as discarded frames. Gaps between the first and last observed queue-output IDs quantify drops only at the tested queue; upstream camera/bridge drops, RTP losses and browser playback drops remain separate.

Also retain:

- Actual queue level (buffers/bytes/time), publisher RSS, and total process CPU/RSS samples.
- Browser presented/decoded FPS, presentation gaps, reported freeze counts/durations and packet/playback drops.
- Qt rendered FPS from actual monotonic intervals.
- Recorder active state, finalization, received/written frames and queue-drop count.
- Actual local/server MP4 SHA-256, size and FFprobe decoded-frame counts.
- Camera settings, hardware/software versions, source/binary fingerprints, full trial schedule and pipeline descriptions without authorization tokens.

The measured stream mode must match F450 1280 x 704. Counter resets, missing required counters, lost PTS matching, nonuniform-image failure or service/recorder termination invalidate the trial rather than becoming zero-valued metrics.

## Interpretation Before The Measured Run

Hypothesis: the latest-frame queue reduces waiting-time tails during controlled slowdown while bounding its occupancy; under normal load the two policies may be similar. Record FPS, freezes and discarded frames alongside latency. A lower queue p95 alone is not proof of universally better video quality. Completing the test is not itself a positive optimization result.

Report any benefit only for this queue, configuration and load. Do not claim an identical percentage reduction in camera-to-screen delay, universal architectural optimality, a GPU speedup, or zero network loss. No application-specific acceptability threshold has been established; usability trade-offs must be described rather than retrospectively declaring a threshold that fits the observations.

## Safety And Reproduction

Abort a trial if publisher RSS exceeds 768 MiB or the queue exceeds 300 buffers. This is a test-abort guard, not a silent cap applied to the baseline. Retain partial evidence. Do not change existing records, reset queues, force statuses, edit persistent env files, or remove recordings. The runner refuses occupied service ports and stops only the services it starts, restoring PostgreSQL to stopped if it was initially stopped.

Build a separate publisher:

```bash
node thesis-documentation/evaluation/buffering-ab/build-ab-publisher.cjs <PROJECT_ROOT> /tmp/ab-build
```

Run the explicitly authorized camera experiment from the diploma root:

```bash
node thesis-documentation/evaluation/buffering-ab/run-buffering-ab.cjs --camera --publisher-binary /tmp/ab-build/ab-whip-client
```

Use `--pilot` for the short pilot. Each invocation needs a fresh output directory. The runner temporarily points the local services at localhost via process environment; the saved LAN `.env` files remain unchanged. The Word thesis is outside this task.
