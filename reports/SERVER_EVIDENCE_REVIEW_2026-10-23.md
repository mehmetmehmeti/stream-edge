# Returned Server Evidence Review: 23 October 2026

## Scope

Offline review of the files supplied from the second laptop. The original server verifier report was captured on 22 October at 18:28:17 UTC; host/service evidence was captured at 18:32:08 UTC. These are historical records, not a check of services currently running on 23 October. No application, database, camera or server was restarted. No supplied executable was run and no Word file was edited.

The files are preserved under `.runtime/server-evidence/2026-10-23/received/`. `returned-server-evidence-audit-2026-10-23.json` retains comparisons and source hashes. This session checked the returned server measurements against the original edge manifest; the server MP4s themselves were not transferred or rehashed on this machine.

## Package And Provenance

- Returned `expected-server-evidence.json` and `verify-lan-on-server.cjs` are byte-for-byte identical to the versions originally supplied by the edge session.
- All 31 available files listed in `SHA256SUMS.txt` match their declared checksums; none mismatch.
- Three checksum-listed files were not supplied: `VERIFICATION-SUMMARY.md`, `postgres.txt`, and `server-lan-verification.json`.
- `supplied-verifier-original-report.json` is present and contains the completed original verifier output. Missing the alternate-named verification report does not negate these available checks. Any additional claims in the missing summary/report remain unreviewed.
- Checksums establish package consistency, not authenticated proof of when or where a report was produced. Host identity, paths, service ownership and captured database/file measurements are mutually consistent in the available evidence.

## Cross-Checked Results

| Check | Result |
|---|---|
| Selected recording hashes, file sizes and decoded frame counts | 31/31 agree with the original edge manifest |
| MP4 format and duration | 31/31 agree with expected H.264, 1280 x 704 and FFprobe durations |
| Total decoded frames | 3,937: 330 in the short check and 3,607 across 30 cycles |
| Completed commands and command versions | 124/124 agree |
| Ended streams | 31/31 |
| Selected server synchronization entries | 496 inbox and 281 outbox, all APPLIED |
| Server rows dated 15 October, Belgrade-date check | 0 streams, 0 videos, 0 commands |

The selected database rows corroborate recording status, stream association, file path, size, frame count and current version. The server has 33 video IDs overall: the manifest intentionally selects 31 accepted recordings and excludes two earlier preparation-attempt recordings. No records or files were deleted during review.

The global inbox additionally lists 23 IGNORED entries. This does not contradict all selected entries being APPLIED, but the whole database must not be described as containing only APPLIED events.

## Video Version Evolution

The returned original verifier checks command versions but does not compare video versions with the manifest. This review did that additional comparison.

Two video rows are version 6 in both returned server sources, while the original manifest records their earlier completed version 5:

| Cycle | Video ID | Manifest | Returned Server | Edge History |
|---|---|---:|---:|---|
| 6 | d4127b7b-d029-43f6-9d78-bde948acd876 | 5 | 6 | completed version-6 update APPLIED |
| 24 | 06c662ff-3e50-4890-aa78-8524844cfd1b | 5 | 6 | completed version-6 update APPLIED |

The saved edge run `2026-10-22T17-58-29-117Z/result.json` already contains those version-6 inbox events, respectively `9253a0b9-60af-423a-9174-5d12125d626f` and `8976d075-3211-44fa-a750-98acfc9d7586`, both APPLIED and completed. The per-cycle API snapshots recorded version 5 earlier. Thus the returned version-6 state is consistent with the edge's recorded later update history; it is not evidence of an unresolved replica disagreement. The original manifest and raw results were not rewritten. The cause of the additional updates is not established by these records and is not attributed to a particular retry or defect.

All other 29 recording versions exactly match the manifest. File hashes, sizes and decoded frame counts match for all 31, including these two recordings.

## Server Environment

- Host: `mehmet-Legion`, IPv4 `192.168.3.162`, architecture `x86_64`.
- OS: Ubuntu 26.04.1 LTS; Node.js: v22.23.1.
- The application database returned PostgreSQL **16.15**. The Node backend's captured connections use port 55432. An earlier 18.6 note must not be used as this application's measured database version; any separate system installation is not established here.
- At capture, backend-web, frontend-web and media-server were active under user `mehmet`, with matching systemd/process PIDs and project working directories. Captured listeners include 3000, 3001, 3010 and media TCP/UDP 40000.
- Backend commit: `e74beef25f48393e4b8ad148202d703c34af56d5`, clean captured status.
- Frontend commit: `cf15556d405471688897e43931d980c5728e637d`, captured change `M next-env.d.ts`. This was not a completely clean three-repository deployment.
- Media commit: `27d02f566deaa6e4d733bac18f732cd64895ef96`, clean captured status.

The initially supplied files did not contain a server CPU model, RAM capacity or GPU specification. The user subsequently supplied `lscpu`, `free -h` and `lspci -nn` output on 23 October, identified as belonging to this second laptop. The raw hardware paste and structured transcription are retained under `.runtime/server-evidence/2026-10-23/hardware/`.

| Server Hardware | Supplied Output |
|---|---|
| CPU | 13th Gen Intel Core i7-13700HX |
| CPU topology | 1 socket, 16 reported cores, 24 online logical CPUs |
| RAM | Approximately 30 GiB total visible to the OS (`free -h`: `30Gi`) |
| Swap | 8.0 GiB |
| Discrete graphics | NVIDIA AD106M, GeForce RTX 4070 Max-Q / Mobile, PCI ID 10de:2860 |
| Integrated graphics | Intel Alder Lake-HX GT1, UHD Graphics 770 PCI label, ID 8086:4688 |

RAM is recorded as the rounded OS-reported total, not an independently verified 32 GB module inventory. This is sufficient to describe the tested host without inventing installed capacity. The heterogeneous CPU's reported 16-core and 24-logical-CPU counts are retained directly; the threads-per-core field must not be multiplied across every core. GPU identification does not establish GPU-accelerated recording or timed server GPU utilization. The later `free` snapshot is not substituted for benchmark resource measurements. Exact capture time and hostname are absent from the hardware paste; host attribution is based on the user's identification.

## Cleanup Evidence

The returned cleanup record reports deletion of 53 commands, 17 video rows and 11 stream rows from 15 October. Backup location:

`<SERVER_PROJECT_ROOT>/deployment/backups/pre-evaluation-20261022T175450309Z`

It records a 218,435-byte database dump with an archive-read check and a recordings archive containing one file. Sixteen referenced recordings were already missing at backup time (`missing_reference_count: 16`); this is not evidence that all 17 server recording files were backed up. The backup archives themselves were not supplied for independent inspection. Post-cleanup evidence records zero remaining old rows and retained users/devices/synchronization sequences. These cleanup facts do not change the integrity results for the selected new LAN recordings.

## Additional Pasted Supplemental Report

The user subsequently supplied the full JSON report as an attachment named `Pasted text.txt`. Its parsed base fields are identical to `supplied-verifier-original-report.json` except for the top-level status. Formatting that parsed JSON with two-space indentation and a final newline reproduces SHA-256 `dc55ffa2bae6da825968e30a72ec379d35ca4b14f6586ce39bc4da72af552611`, exactly the package checksum for the initially missing `server-lan-verification.json`. The raw paste and reconstructed JSON are retained separately under the evidence directory's `supplement/`; no field values were changed. The initial receipt audit remains unchanged as a record of what was supplied at that point.

The supplemental report retains `provided_verifier_status: passed` but sets overall/supplemental `status: failed`, because exact video-version equality with the manifest fails for the same two version-5/version-6 rows described above. No other supplemental checks fail. That failed status is preserved, not relabelled as passed. The cross-report conclusion is narrower: all file integrity and command checks agree, and the later video versions are corroborated by already-saved edge APPLIED events. This is not a claim that the server's exact-manifest-version check passed.

The supplement confirms 31 file-integrity results, 124 commands and 31 ended streams; it also distinguishes database-duration fields from actual MP4 timeline durations. That JSON adds no hardware inventory and explicitly says physical-server browser playback, seek and fullscreen were not observed. Hardware was subsequently supplied separately, as recorded above. Of the original checksum list, only `VERIFICATION-SUMMARY.md` and `postgres.txt` remain unavailable; application PostgreSQL information is already in the reviewed JSON.

## Non-Word Status: Items 18-25

- Item 20: server on-disk/database evidence is available and cross-checked. The existing automated playback checks were performed on the edge browser against the server-hosted application. Physical-server browser playback, seek and fullscreen remain unobserved (`server_browser_playback_confirmed` is null); this is optional additional coverage, not a required rerun for the agreed claims. Do not describe the existing browser tests as having run on the second laptop.
- Item 22: server CPU, OS-reported RAM, GPU identification, OS, architecture, application database version, Node version, commits, ownership and listener evidence are available. No further hardware command is required for the scoped environment description; quote RAM as approximately 30 GiB visible to the OS.
- Items 18, 19, 21 and 23: the previous latency, command/startup, repeated recording comparison, long recording and timed counter measurements remain available. This package does not require rerunning them.
- Item 24: conclusions remain approximately 160 ms local / 205 ms web optical delay in the supplied consecutive-frame sample; recording maintained approximately 15 FPS with higher CPU/RSS in the controlled comparison; all 30 LAN cycles completed and file integrity passed, while browser playback/RTP losses were nonzero. No claim of a particular optimization's causal performance improvement is added.
- Item 25: no additional measurements arise from this package; applying measured numbers to the equations remains document work.

The two still-missing package files are an archive-completeness gap, not a reason to rerun the camera benchmark. Do not claim their contents were reviewed.

For the agreed scope and conclusions, non-Word measurement/evidence collection for items 18-25 is complete. No further camera benchmark or phone video is required. Incorporation into the thesis and any stronger causal optimization claims remain separate; this status does not claim that unperformed tests passed or guarantee the professor's acceptance.
