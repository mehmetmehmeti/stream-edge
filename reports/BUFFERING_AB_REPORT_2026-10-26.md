# F450 Buffering A/B Results - 26 October 2026

## Outcome

The repeated real-camera experiment PASSED: 12/12 trials, 48/48 application commands, and 12/12 verified local/server MP4 pairs. This supplies a numerical baseline comparison for professor comment 18(b), specifically the pre-encoder queue policy. It does not test shared capture against two separate captures, nor establish universal architectural optimality.

Under a deliberately induced live-consumer slowdown, the two-buffer downstream-leaky policy reduced the mean of the three trial p95 queue waiting times from **531.363 ms to 129.844 ms**, a **401.519 ms / 75.564% reduction**. All three matched pairs improved. This is a queue-stage result, not a 75.564% reduction in optical camera-to-screen latency.

## Evidence And Setup

- Run ID: `2026-10-26T16-29-37-254Z`; completed at `2026-10-26T16:52:08.237Z`.
- Raw evidence: [`measured-20261026-01`](../../.runtime/buffering-ab/measured-20261026-01/).
- [Protocol](buffering-ab/BUFFERING_AB_PROTOCOL.md), [raw result](../../.runtime/buffering-ab/measured-20261026-01/result.json), [analysis](../../.runtime/buffering-ab/measured-20261026-01/analysis.json), [per-trial CSV](../../.runtime/buffering-ab/measured-20261026-01/comparison.csv).
- [Independent audit](../../.runtime/buffering-ab/measured-20261026-01/independent-audit.json), [hardware](../../.runtime/buffering-ab/measured-20261026-01/hardware.json), [build provenance](../../.runtime/buffering-ab/tools-20261026/build-provenance.json), [shutdown check](../../.runtime/buffering-ab/measured-20261026-01/shutdown-check.json).
- [Evidence checksum inventory](../../.runtime/buffering-ab/measured-20261026-01/evidence-manifest.json) covers the raw run files, evaluation tooling and this report. Original MP4 paths and their independently recomputed hashes are listed in the audit; the recordings are not duplicated into the evidence directory. Operational logs are retained locally and should be reviewed for credentials before external sharing.
- Edge laptop: i5-12450H, 12 logical CPUs, 15.32 GiB OS-visible RAM, Ubuntu 24.04.4 LTS. GPU inventory is recorded but this experiment makes no GPU-acceleration claim.
- Intel F450, 1280 x 704 YUYV, nominal 15 FPS; CPU H.264 recording enabled in every trial. Live output uses the fixed VP8 configuration and actual WHIP/mediasoup/WebRTC browser path, alongside Qt preview.
- Three repetitions per policy and load. Each trial had 15 s warmup and 60 s measurement; actual native measurement windows were 60.006-60.233 s. Each trial started a fresh stream, publisher and recording.
- Normal order: FIFO/latest, latest/FIFO, FIFO/latest. Slow order: latest/FIFO, FIFO/latest, latest/FIFO. Pilot and synthetic validation are separate, not included in the tables.

The experiment was self-contained on one laptop to keep hardware and network topology fixed while isolating buffering. The web receiver was measured directly; its behaviour was not inferred from Qt. The earlier two-laptop evaluation remains separate deployment evidence. These results do not establish identical end-to-end delays on one and two laptops.

## Controlled Change

| Pre-encoder queue | max-size-buffers | max-size-bytes | max-size-time | leaky |
|---|---:|---:|---:|---|
| FIFO baseline | 0 | 0 | 0 | no |
| Latest-frame | 2 | 0 | 0 | downstream |

Zero disables that queue's corresponding limit. Upstream latest-frame stages and the 16 MiB SHM pool stayed fixed, so the baseline is not an entirely unbounded architecture. The finite SHM pool can impose backpressure. No claim of indefinite FIFO memory growth is justified.

Both arms used the same identity stage after the queue: sleep-time=0 normally and sleep-time=100000 microseconds per frame under controlled slowdown. This deliberately adds 100 ms before conversion and encoding. The observed roughly 7 FPS under stress reflects this injected bottleneck plus processing time; it is not normal deployment throughput. Normal-load browser output remained about 15 FPS in both arms. The delay was applied only through the benchmark launcher, not saved production configuration.

## Numerical Comparison

Queue times use GLib monotonic timestamps in the same publisher process, matched by original buffer PTS. The primary interval is queue sink-pad entry to source-pad output. The publisher slice extends from that entry to encoded VP8 output. Both exclude camera capture, earlier bridge/SHM waiting, transport, receiver buffering and display.

Condition values below are arithmetic means of three per-trial statistics, not pooled frame percentiles. Queue occupancy is the maximum sampled level, with samples approximately every 200 ms. The enforced two-buffer limit is independently recorded in each native configuration event.

| Load | Policy | Mean trial median queue ms | Mean trial p95 queue ms | Mean trial p95 publisher slice ms | Browser FPS | Max sampled queued buffers | Qt FPS |
|---|---|---:|---:|---:|---:|---:|---:|
| Normal | FIFO | 0.076 | 0.218 | 41.354 | 15.008 | 1 | 15.011 |
| Normal | Latest-frame | 0.072 | 0.164 | 41.007 | 15.015 | 1 | 14.994 |
| Controlled slowdown | FIFO | 454.022 | 531.363 | 686.133 | 7.066 | 4 | 7.057 |
| Controlled slowdown | Latest-frame | 102.155 | 129.844 | 278.182 | 6.993 | 2 | 15.028 |

| Load | Pair | FIFO p95 queue ms | Latest-frame p95 queue ms | Difference ms | Reduction |
|---|---:|---:|---:|---:|---:|
| Slowdown | 1 | 586.236 | 130.284 | 455.952 | 77.776% |
| Slowdown | 2 | 498.570 | 128.175 | 370.395 | 74.291% |
| Slowdown | 3 | 509.284 | 131.074 | 378.210 | 74.263% |

![Per-trial p95 queue comparison; panel scales differ](../../.runtime/buffering-ab/measured-20261026-01/queue-p95-comparison.png)

## Trade-Offs And Integrity

| Load | Policy | Publisher mean RSS MiB | App/browser CPU % | Summed RSS MiB | Tested-queue skipped IDs | Browser playback drops | Reported freezes | Recorder queue drops | Verified MP4 pairs |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Normal | FIFO | 81.089 | 331.181 | 3103.050 | 0 | 0 | 0 | 0 | 3/3 |
| Normal | Latest-frame | 80.195 | 300.365 | 3155.263 | 0 | 0 | 0 | 0 | 3/3 |
| Controlled slowdown | FIFO | 81.429 | 320.505 | 3211.493 | 0 | 0 | 0 | 0 | 3/3 |
| Controlled slowdown | Latest-frame | 81.421 | 333.433 | 3201.263 | 1441 | 4 | 0 | 0 | 3/3 |

CPU 100% denotes one logical core. These resource totals cover sampled application processes and the dedicated browser, not the whole host; PostgreSQL and the benchmark controller are excluded. Summed RSS can double-count shared pages. CPU/RSS are descriptive secondary observations, not evidence of a demonstrated resource-efficiency improvement.

The latest-frame policy deliberately skipped 463, 484 and 494 old live frames across the three slowdown trials, respectively 51.39%, 53.66% and 54.77% of the IDs between the first and last observed queue outputs. These are tested-queue gaps, not camera-to-display total loss. FIFO's zero gaps at this stage do not prove zero upstream drops. Startup and pending tail are excluded, and the two profiles need not admit equal numbers of frames upstream under backpressure.

Browser playback reported 3 dropped frames in slowdown/latest repetition 2 and 1 in repetition 3. These are distinct from the intentional queue discards. All measured RTP packet-loss deltas and reported freeze deltas were zero. Absence of a reported freeze does not mean continuous 15 FPS: both stressed web outputs were around 7 FPS. Mean trial p95 browser presentation gaps were 164.33 ms for stressed FIFO and 178.27 ms for stressed latest-frame; no web smoothness improvement is claimed. Gap sampling follows the browser measurement loop and can extend slightly beyond the exact native marker window.

Qt preview remained around 15 FPS with the bounded policy under slowdown, compared with about 7 FPS for FIFO. This is consistent with reduced upstream backpressure in the shared live path; the precise causal contribution was not separately instrumented. It must not be described as increased web FPS.

The recorder received and wrote equal counts in every trial, with zero reported recorder queue drops and 14,076 decoded frames across all 12 finalized MP4s. Actual local and server files passed SHA-256 and byte-size comparisons; the runner checked decoded-frame counts on both replicas, and the independent audit recomputed all 24 file hashes and checked logged decoded counts against recorder counts. This verifies recording integrity within the observed diagnostics, not that every physical camera frame was necessarily captured.

## Interpretation For Comment 18(b)

This experiment supports a low-latency-oriented optimization: when the live consumer falls behind, the bounded downstream-leaky queue trades old live frames for lower waiting-time tails while the independent recording path continues successfully. The benefit appeared in every stressed pair. It does not restore the consumer's processing capacity or improve stressed browser FPS.

Under normal load, both arms had sub-millisecond queue waiting and approximately 15 FPS; the small queue-time difference is not evidence of a meaningful end-to-end benefit. Under stress, publisher RSS was essentially identical (81.429 versus 81.421 MiB). Therefore this run does not demonstrate meaningful RAM savings or unlimited memory growth in FIFO. Only three short repetitions per condition were collected; no statistical significance, long-duration guarantee, application-specific acceptability threshold or universal optimality is claimed.

The prior optical local/web photographs remain separate estimates. They cannot be combined with these queue timings to calculate a new end-to-end latency or assign a network-only component.

### Suggested Thesis Conclusion

Ne kushtet e testuara, kufizimi i radhes para enkodimit ne dy kuadro me heqje te kuadrove me te vjetra uli vonesen e pritjes gjate ngadalesimit te kontrolluar. Mesatarja e percentileve te 95-ta te tri perseritjeve ra nga 531.363 ms ne 129.844 ms, nje ulje prej 75.564%. Ky perfitim u arrit duke hequr kuadro te vjetra nga dega live, jo duke rritur kapacitetin e perpunimit: transmetimi ne shfletues mbeti rreth 7 FPS nen ngarkesen e imponuar, ndersa ne ngarkese normale te dy konfigurimet arriten rreth 15 FPS. Te 12 regjistrimet perfunduan me perputhje te skedareve lokal/server dhe pa humbje te raportuara ne radhen e regjistrimit. Rezultatet mbeshtesin nje optimizim te orientuar drejt voneses se ulet ne kushtet e mbingarkeses se testuar; ato nuk perfaqesojne matje te voneses totale nga kamera deri ne ekran dhe nuk provojne ulje domethenese te perdorimit te RAM-it.

This ASCII Albanian paragraph is preparation text outside Word; restore standard Albanian diacritics when incorporating it into the thesis.

## Verification And Handoff

- Separate native instrumentation passed synthetic PTS/queue tests and four real-camera pilot trials before the measured run. These observations were not pooled into the results above.
- Analyzer unit checks passed for median/p95, warmup boundaries, pending tails, true skips, missing markers, safety abort, unmatched PTS and reordered output.
- Independent audit passed all 12 measured trials, unique/valid input PTS, input/output timestamp matching, source/binary fingerprints, measured conditions, all 48 completed commands, and actual recording file hashes.
- Actual viewer screenshots and per-trial token-free pipeline descriptions are retained with native logs. Representative: [normal latest-frame](../../.runtime/buffering-ab/measured-20261026-01/02-normal-latest/viewer.png), [slow latest-frame](../../.runtime/buffering-ab/measured-20261026-01/07-slow-latest/viewer.png), [slow FIFO](../../.runtime/buffering-ab/measured-20261026-01/08-slow-fifo/viewer.png). Screenshots document the scene/application state, not the numerical timing by themselves.
- Test-owned backends, web frontend, media server, streaming service and PostgreSQL were stopped. The saved check confirms no listeners on 3000, 3001, 3002, 3010 or 8081 and no remaining recorded service PIDs.
- Production queue implementation, production WHIP binary and persistent environment files were not edited for this experiment. No existing database rows or recordings were deleted. New benchmark accounts, streams, recordings and raw evidence remain available.
- All six application repositories remain clean at their pre-run commits; the saved [post-run repository check](../../.runtime/buffering-ab/measured-20261026-01/post-run-repositories.json) records them. Camera-device checks found no remaining F450 users after shutdown.
- No Word edits, commits or pushes were performed. Comment 18(b)'s measurement/conclusion evidence is prepared; insertion into Word remains separate.
