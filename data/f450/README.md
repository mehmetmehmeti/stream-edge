# F450 Recording And Resource Measurements

Run date: 15 October 2026. `20261015T173020Z/` contains three alternating recording-off/on pairs and the long recording's retained measurements. Capture was 1280 x 704 YUYV, nominal 15 FPS; recording used CPU H.264.

[Hardware and installed software inventory](20261015T173020Z/hardware-inventory.json) supports Tables 3-4, including the edge CPU/GPU, OS-visible RAM, Qt/Holoscan/CUDA and application versions. It is a field-selected derivative of the retained run inventory; identities and unrestricted machine output are omitted. [Projection source](../../scripts/project-f450-inventory.py) documents the extraction.

Start with [summary.json](20261015T173020Z/summary.json), then [benchmark-result.json](20261015T173020Z/benchmark-result.json). The same directory contains the available browser, Qt, resource and transfer samples. The [dated report](../../reports/F450_RUN_REPORT_2026-10-15.md) explains intervals, units and interpretation. The public verifier is [verify-measurements.py](../../scripts/verify-measurements.py).

The long file measured 616.180 s, 563,137,159 bytes and 9,255 decoded frames. CPU replicate means were 58.745% without and 206.528% with recording, with 100% representing one logical core. Resource samples and Qt monotonic counters support the calculations. The raw recordings remain private by author choice: public readers can inspect the reports but cannot independently rehash or decode the MP4s. [Recording attestation](../recording-attestation.json) records the separate 29 October read-only recheck.

The application trees were dirty during this historical run. Base commits and diff hashes are retained, but they are not the full measured source patch. This workload is separate from the LAN and optical datasets.
