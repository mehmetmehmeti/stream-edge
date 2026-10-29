# Public Measurement Derivatives

This is the measurement stage's preparation record. Commands use the repository root as the working directory. See the integrated [F450 index](../data/f450/README.md), [A/B index](../data/buffering-ab/README.md) and [root index](../README.md) for the complete publication. Exclusion statements below apply to this stage only.

Only the completed F450 run (15 October), measured buffering A/B run (26 October), and cited camera-only preflight are included. Original files were read-only. No live experiments, services, camera access, builds or CTest were run.

Run `python3 -B scripts/verify-measurements.py` from the repository root. It uses only public staged files and the Python standard library. It recomputes F450 browser/Qt/resource summaries, native A/B queue statistics, grouping arithmetic and the preflight driver FPS; it cannot recreate missing final A/B browser snapshots.

In the parent package use `python3 scripts/verify-measurements.py --integrated`; the verifier also auto-detects provenance/measurement-stage-provenance.json. Integrated mode maps provenance.json, recording-attestation.json and verification-result.json to provenance/measurement-stage-provenance.json, data/recording-attestation.json and provenance/measurement-stage-verification-result.json respectively. It checks only its manifest-owned file hashes and scans only its own data for private content. Other package files are left to the parent's global verifier. Manifest paths may use either the original stage names or those integrated names.

Under induced slowdown, mean trial p95 queue waiting changed from 531.363 to 129.844 ms (75.564% reduction). This is a pre-encoder queue result, not optical latency; stressed browser output remained about 7 FPS. Recording-off/on CPU means in the F450 run were 58.745/206.528%, with 100% denoting one logical core. Summed RSS may double-count shared memory. GPU counters are host-wide.

Every source data file is projected through explicit approved fields and known safe categorical values. Credentials, email, host/network identities, unrestricted text, absolute paths and process IDs are omitted. Numeric measurement values are not perturbed. RTP IDs and recording IDs are portable synthetic labels. The raw driver log is reduced to numeric buffer records. Pipeline socket locations are replaced; historical pipeline/source/build hashes are preserved and derivative pipeline hashes are separate. Parent publishes exact modified WHIP C source, debug.h and GPLv3 LICENSE with baseline diff.

Private MP4s are NOT included. recording-attestation.json records fresh SHA256/size equality for 46 retained local recordings and retained FFprobe fields. Without media the public cannot redo decoding or byte-hash verification. Thirty cycle records appear only as media attestations; cycle measurements belong to the LAN agent. Neither this attestation nor the derivative provenance authenticates original capture dates.

Limitations: A/B raw sample logs omit terminal browser snapshots; endpoint-derived browser metrics remain recorded results, not independently raw-recomputable endpoints. F450 application trees were dirty: recorded base commits/diff hashes alone do not reconstruct their exact measured sources. No optical or server-deployment conclusions are made.

Historical CTest is separate. Parent located an actual 15 October log reporting 6/6 passed; this derivative does not duplicate or independently validate that log. The 29 October public verification is offline arithmetic only, never a relabelled historical test.

Excluded: pilot/synthetic trial datasets, failed/interrupted attempts, integrated-camera historical baselines, historical4, optical data, full LAN/server evidence, operational logs, environment/auth files, private Qt settings, dumps, builds, binaries, screenshots and videos. Per-file provenance records original and published SHA256 with relative source labels; parent supplies the global publication manifest.
