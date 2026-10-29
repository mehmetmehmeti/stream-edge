# Optical Live-Stream Latency: 22 October 2026

## Method

All 31 local-stream JPEGs and all 28 web-stream JPEGs supplied by the user were inspected, including native-resolution timer crops from every picture. The upper timer is the direct reference and the lower timer is its streamed image. Route labels follow the user's local/web labels; the full-screen photographs alone do not establish the player implementation or capture/encoding configuration.

For each legible pair: latency_ms = source_stopwatch_ms - streamed_stopwatch_ms. This compares the same stopwatch seen directly and through the capture/display path; no synchronization of two computer clocks is used. This is an optical source-display-to-stream-display estimate, not a network RTT, command-completion time, startup time, or an isolated network/codec delay.

The user described consecutive phone-captured frames/pictures and confirmed a 60 Hz reference monitor. Exact capture/export mode, phone frame cadence, exposure, scan timing and receiver refresh rate were not established. Attachment order is preserved in the CSV, not treated as chronological order. Accepted readings are descriptive observations of these supplied frames, not independent trial runs.

## Results

| User-labelled route | Legible frames | Mean (ms) | p50 (ms) | p95 (ms) | Observed range (ms) |
|---|---:|---:|---:|---:|---:|
| Local live stream | 25 | 160.4 | 172 | 172 | 114-172 |
| Web live stream | 21 | 204.5 | 229 | 229 | 171-229 |

The median is the middle observation (or mean of the two middle observations). p95 uses nearest rank: sorted[ceil(0.95*n)-1]. Integer differences and one-decimal means are retained for arithmetic traceability, not as a statement of instrument accuracy. Rounded presentation values are approximately 160 ms locally and 205 ms in the web viewer.

All 59 supplied latency pictures are listed in readings.csv, including exclusions. Local ambiguous images: #1, #4, #13, #18, #21, #25 (6). Web ambiguous images: #1, #3, #11, #13, #16, #20, #24 (7). A picture is excluded when overlapping timer digits prevent a unique reading, never on the basis of the measured delay. Minor shadowing is retained only where the digits remain legible and is noted per row. No ambiguous digits were filled from neighboring images or from the prevalent delay pattern.

## Sampling And Repeated Values

0 byte-identical duplicates were found in the local set and 0 in the web set. Repeated legible displayed pairs occur in local #9/#10 and #26/#27, and web #14/#15 and #22/#23. Different JPEG hashes do not prove independent capture instants. The primary summary retains every accepted supplied frame, with repeated pairs explicitly marked.

Sensitivity when retaining only the first accepted occurrence of each displayed timer pair:

| User-labelled route | Distinct pairs | Mean (ms) | p50 (ms) | p95 (ms) | Observed range (ms) |
|---|---:|---:|---:|---:|---:|
| Local live stream | 23 | 159.4 | 172 | 172 | 114-172 |
| Web live stream | 19 | 201.9 | 229 | 229 | 171-229 |

This sensitivity analysis does not convert consecutive observations into independent trials. The direct local stopwatch values span 00:00:09.566 to 00:00:11.055 (1.489 s of stopwatch time). The web pictures contain two distinct stopwatch ranges: 00:00:09.337-00:00:09.452 and approximately 00:01:35.470-00:01:36.730. Their capture spacing/reset history is not known; the gap is not treated as an observed test duration. These samples describe short supplied sequences rather than a long-duration latency distribution, and no confidence interval is asserted.

The web mean exceeds the local mean by 44.1 ms in these separate samples. The observations are not paired captures of the same camera frame under verified identical conditions; the difference must not be labelled the network-only or server-only delay.

## Resolution And Uncertainty

The confirmed 60 Hz reference monitor has a refresh period of 1000/60 = 16.67 ms. That period alone is not a bound on total measurement uncertainty. Although three decimal places are displayed, readable consecutive stopwatch values commonly differ by about 57-58 ms. This is an observed timer-value spacing in these pictures, not a calibrated 1 ms measurement resolution or proof of the timer's complete update schedule.

Phone exposure/readout, possible rolling-shutter effects, timer/display refresh phase, camera sampling and receiver presentation can affect the photographed difference; mixed digits visibly occur in both sets. The receiver refresh and phone timing are unknown, so a numerical total uncertainty such as +/-1 ms or +/-16.7 ms is not justified. No guessed correction or refresh-period subtraction has been applied. Reporting approximately 160 ms and 205 ms with this method description is appropriate for these data.

## Figure Choices

Local latency: #7 (f081b63b-8b23-4357-bae9-11ddd023020a.jpeg) has clear 00:00:10.769 and 00:00:10.597 readings, a displayed difference of 172 ms.

Web latency: #10 (111ee65e-39c9-42d6-a8b2-be398b058dac.jpeg) has clear 00:01:36.730 and 00:01:36.501 readings, a displayed difference of 229 ms. These examples illustrate the measurement; the summary uses all accepted readings, not just the figures.

Two-laptop setup: #3 (fc484d60-26e3-4332-980f-b77ca1fc2065.jpeg) remains the preferred setup photograph for its lighting and visibility of both screens. It is not a timer measurement.

## Evidence And Reproduction

Evidence directory: .runtime/optical-latency/2026-10-22. readings-input.json retains the manual transcription, readings.csv contains every row with source path, SHA-256, acceptance status and calculated difference, and summary.json contains the statistics and method metadata. The original JPEGs remain unchanged in the two user-provided Desktop folders; only inspection crops were made under /tmp. Source SHA-256 values are checked on reproduction. Preserve those source folders with the evidence files.

From the project root:

```bash
node thesis-documentation/evaluation/analyze-optical-latency.cjs .runtime/optical-latency/2026-10-22/readings-input.json /tmp/optical-latency-recheck
```

The script refuses to overwrite an existing output directory. It reproduces arithmetic from manual transcriptions; it does not perform OCR or independently validate the transcribed digits. The Word thesis has not been edited by this analysis.

## Tekst Per Punim

Vonesa optike u vleresua duke fotografuar ne te njejten pamje kronometrin referues dhe imazhin e tij ne transmetim. Nga 31 pamjet e transmetimit lokal dhe 28 pamjet e shikuesit ueb, u perdoren perkatesisht 25 dhe 21 cifte me shifra te lexueshme. Vonesa mesatare e vleresuar ishte rreth 160 ms per transmetimin lokal dhe 205 ms per shikuesin ueb; mediana ishte perkatesisht 172 ms dhe 229 ms, ndersa percentili i 95-te ishte 172 ms dhe 229 ms. Intervalet e vezhguara ishin 114-172 ms dhe 171-229 ms. Pamjet jane marre ne vazhdimesi dhe nuk trajtohen si prova te pavarura. Monitori referues punonte ne 60 Hz, me periudhe rifreskimi 16.67 ms. Shifrat dhjetore te kronometrit nuk nenkuptojne saktesi matjeje prej 1 ms; pasiguria e pergjithshme nuk u kuantifikua, pasi parametrat kohore te telefonit dhe ekranit marres nuk u percaktuan. Pamjet me shifra te mbivendosura u perjashtuan nga llogaritjet.
