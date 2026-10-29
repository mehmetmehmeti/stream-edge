# Pre-Encoder Queue A/B

Run date: 26 October 2026. Twelve camera trials cover FIFO and two-buffer downstream-leaky policies under normal load and an identical injected 100 ms/frame slowdown. Each condition has three trials, 15 s warmup and 60 s observation; recording stays enabled.

Start with [analysis.json](measured-20261026-01/analysis.json) and [result.json](measured-20261026-01/result.json). Each trial contains native PTS-matched probe events, available browser samples and its pipeline description. [Protocol](../../thesis-documentation/evaluation/buffering-ab/BUFFERING_AB_PROTOCOL.md), [dated report](../../reports/BUFFERING_AB_REPORT_2026-10-26.md), and [offline verifier](../../scripts/verify-measurements.py) give the method and checks.

The mean of trial p95 queue waiting fell from 531.363 to 129.844 ms under slowdown, a 75.564% reduction. The bounded policy skipped 1,441 older live frames. Both stressed browser arms stayed near 7 FPS; publisher RSS was essentially unchanged. This is a queue-stage trade-off, not a measured 75.564% optical end-to-end improvement or a RAM-saving result.

The [exact modified WHIP client](../../sources/simple-whip-client/src/whip-client.c), its GPLv3 license and local patch are supplied separately from the native probe. Historical source/build hashes and public derivative pipeline hashes are distinct. No prebuilt binary or MP4 is distributed. Missing terminal browser snapshots prevent full reconstruction of endpoint-derived browser metrics; native queue timing is reconstructable from the probe records.
