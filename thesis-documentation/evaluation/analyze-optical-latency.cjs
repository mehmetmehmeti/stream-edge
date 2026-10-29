'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');

function clock(ms) {
  if (ms === null) return '';
  return `${String(Math.floor(ms / 3600000)).padStart(2,'0')}:${String(Math.floor(ms / 60000) % 60).padStart(2,'0')}:${String(Math.floor(ms / 1000) % 60).padStart(2,'0')}.${String(ms % 1000).padStart(3,'0')}`;
}

function stats(values) {
  assert(values.length > 0);
  const sorted = [...values].sort((a,b) => a-b);
  const n = sorted.length;
  return {
    n,
    mean_ms: values.reduce((sum,v) => sum+v,0) / n,
    p50_ms: n % 2 ? sorted[(n-1)/2] : (sorted[n/2-1]+sorted[n/2])/2,
    p95_nearest_rank_ms: sorted[Math.ceil(0.95*n)-1],
    min_ms: sorted[0],
    max_ms: sorted[n-1],
    histogram_ms: Object.fromEntries([...new Set(sorted)].map(v => [v,values.filter(x => x === v).length]))
  };
}

function csvCell(value) {
  return `"${String(value ?? '').replaceAll('"','""')}"`;
}

function analyze(input) {
  const rows = [];
  const summaries = {};
  for (const set of input.sets) {
    const filenames = new Set();
    const seenPairs = new Map();
    const analyzed = set.rows.map((tuple,offset) => {
      assert.equal(tuple.length,5);
      const [stem,source_ms,streamed_ms,status,note] = tuple;
      const filename = stem+'.jpeg';
      assert(!filenames.has(filename));
      filenames.add(filename);
      for (const ms of [source_ms,streamed_ms]) assert(ms === null || (Number.isSafeInteger(ms) && ms >= 0));
      assert(['accepted','ambiguous'].includes(status));
      const accepted = status === 'accepted';
      if (accepted) assert(source_ms !== null && streamed_ms !== null && source_ms >= streamed_ms);
      else assert(source_ms === null || streamed_ms === null);
      const source_path = path.join(set.source_directory,filename);
      const bytes = fs.readFileSync(source_path);
      const pair = accepted ? `${source_ms}:${streamed_ms}` : null;
      const repeated_pair_of = pair === null ? null : (seenPairs.get(pair) ?? null);
      if (pair !== null && !seenPairs.has(pair)) seenPairs.set(pair,offset+1);
      return {
        set: set.id, image_number: offset+1, filename, source_path,
        sha256: crypto.createHash('sha256').update(bytes).digest('hex'), file_bytes: bytes.length,
        source_clock: clock(source_ms), streamed_clock: clock(streamed_ms), source_ms, streamed_ms,
        status, latency_ms: accepted ? source_ms-streamed_ms : null, repeated_pair_of, note
      };
    });
    const accepted = analyzed.filter(r => r.status === 'accepted');
    const distinct = accepted.filter(r => r.repeated_pair_of === null);
    const hashes = analyzed.map(r => r.sha256);
    summaries[set.id] = {
      supplied_images: analyzed.length,
      accepted_images: accepted.length,
      excluded_ambiguous_images: analyzed.filter(r => r.status === 'ambiguous').map(r => r.image_number),
      byte_identical_duplicate_count: hashes.length-new Set(hashes).size,
      repeated_displayed_pairs: accepted.filter(r => r.repeated_pair_of !== null).map(r => ({image_number:r.image_number, same_pair_as:r.repeated_pair_of})),
      descriptive_frame_statistics: stats(accepted.map(r => r.latency_ms)),
      distinct_displayed_pair_sensitivity: stats(distinct.map(r => r.latency_ms)),
      source_clock_values_ms: [...new Set(analyzed.map(r => r.source_ms).filter(v => v !== null))].sort((a,b) => a-b)
    };
    rows.push(...analyzed);
  }
  return {rows, summaries};
}

function report(input, result) {
  const local = result.summaries.local;
  const web = result.summaries.web;
  const a = local.descriptive_frame_statistics;
  const b = web.descriptive_frame_statistics;
  const f = x => x.toFixed(1);
  const tableRow = (label,s) => `| ${label} | ${s.n} | ${f(s.mean_ms)} | ${s.p50_ms} | ${s.p95_nearest_rank_ms} | ${s.min_ms}-${s.max_ms} |`;
  return `# Optical Live-Stream Latency: 22 October 2026

## Method

All 31 local-stream JPEGs and all 28 web-stream JPEGs supplied by the user were inspected, including native-resolution timer crops from every picture. The upper timer is the direct reference and the lower timer is its streamed image. Route labels follow the user's local/web labels; the full-screen photographs alone do not establish the player implementation or capture/encoding configuration.

For each legible pair: latency_ms = source_stopwatch_ms - streamed_stopwatch_ms. This compares the same stopwatch seen directly and through the capture/display path; no synchronization of two computer clocks is used. This is an optical source-display-to-stream-display estimate, not a network RTT, command-completion time, startup time, or an isolated network/codec delay.

The user described consecutive phone-captured frames/pictures and confirmed a 60 Hz reference monitor. Exact capture/export mode, phone frame cadence, exposure, scan timing and receiver refresh rate were not established. Attachment order is preserved in the CSV, not treated as chronological order. Accepted readings are descriptive observations of these supplied frames, not independent trial runs.

## Results

| User-labelled route | Legible frames | Mean (ms) | p50 (ms) | p95 (ms) | Observed range (ms) |
|---|---:|---:|---:|---:|---:|
${tableRow('Local live stream',a)}
${tableRow('Web live stream',b)}

The median is the middle observation (or mean of the two middle observations). p95 uses nearest rank: sorted[ceil(0.95*n)-1]. Integer differences and one-decimal means are retained for arithmetic traceability, not as a statement of instrument accuracy. Rounded presentation values are approximately ${Math.round(a.mean_ms)} ms locally and ${Math.round(b.mean_ms)} ms in the web viewer.

All ${local.supplied_images+web.supplied_images} supplied latency pictures are listed in readings.csv, including exclusions. Local ambiguous images: ${local.excluded_ambiguous_images.map(n=>'#'+n).join(', ')} (${local.excluded_ambiguous_images.length}). Web ambiguous images: ${web.excluded_ambiguous_images.map(n=>'#'+n).join(', ')} (${web.excluded_ambiguous_images.length}). A picture is excluded when overlapping timer digits prevent a unique reading, never on the basis of the measured delay. Minor shadowing is retained only where the digits remain legible and is noted per row. No ambiguous digits were filled from neighboring images or from the prevalent delay pattern.

## Sampling And Repeated Values

${local.byte_identical_duplicate_count} byte-identical duplicates were found in the local set and ${web.byte_identical_duplicate_count} in the web set. Repeated legible displayed pairs occur in local #9/#10 and #26/#27, and web #14/#15 and #22/#23. Different JPEG hashes do not prove independent capture instants. The primary summary retains every accepted supplied frame, with repeated pairs explicitly marked.

Sensitivity when retaining only the first accepted occurrence of each displayed timer pair:

| User-labelled route | Distinct pairs | Mean (ms) | p50 (ms) | p95 (ms) | Observed range (ms) |
|---|---:|---:|---:|---:|---:|
${tableRow('Local live stream',local.distinct_displayed_pair_sensitivity)}
${tableRow('Web live stream',web.distinct_displayed_pair_sensitivity)}

This sensitivity analysis does not convert consecutive observations into independent trials. The direct local stopwatch values span 00:00:09.566 to 00:00:11.055 (1.489 s of stopwatch time). The web pictures contain two distinct stopwatch ranges: 00:00:09.337-00:00:09.452 and approximately 00:01:35.470-00:01:36.730. Their capture spacing/reset history is not known; the gap is not treated as an observed test duration. These samples describe short supplied sequences rather than a long-duration latency distribution, and no confidence interval is asserted.

The web mean exceeds the local mean by ${f(b.mean_ms-a.mean_ms)} ms in these separate samples. The observations are not paired captures of the same camera frame under verified identical conditions; the difference must not be labelled the network-only or server-only delay.

## Resolution And Uncertainty

The confirmed 60 Hz reference monitor has a refresh period of 1000/60 = 16.67 ms. That period alone is not a bound on total measurement uncertainty. Although three decimal places are displayed, readable consecutive stopwatch values commonly differ by about 57-58 ms. This is an observed timer-value spacing in these pictures, not a calibrated 1 ms measurement resolution or proof of the timer's complete update schedule.

Phone exposure/readout, possible rolling-shutter effects, timer/display refresh phase, camera sampling and receiver presentation can affect the photographed difference; mixed digits visibly occur in both sets. The receiver refresh and phone timing are unknown, so a numerical total uncertainty such as +/-1 ms or +/-16.7 ms is not justified. No guessed correction or refresh-period subtraction has been applied. Reporting approximately ${Math.round(a.mean_ms)} ms and ${Math.round(b.mean_ms)} ms with this method description is appropriate for these data.

## Figure Choices

Local latency: #7 (f081b63b-8b23-4357-bae9-11ddd023020a.jpeg) has clear 00:00:10.769 and 00:00:10.597 readings, a displayed difference of 172 ms.

Web latency: #10 (111ee65e-39c9-42d6-a8b2-be398b058dac.jpeg) has clear 00:01:36.730 and 00:01:36.501 readings, a displayed difference of 229 ms. These examples illustrate the measurement; the summary uses all accepted readings, not just the figures.

Two-laptop setup: #3 (fc484d60-26e3-4332-980f-b77ca1fc2065.jpeg) remains the preferred setup photograph for its lighting and visibility of both screens. It is not a timer measurement.

## Evidence And Reproduction

Evidence directory: .runtime/optical-latency/2026-10-22. readings-input.json retains the manual transcription, readings.csv contains every row with source path, SHA-256, acceptance status and calculated difference, and summary.json contains the statistics and method metadata. The original JPEGs remain unchanged in the two user-provided Desktop folders; only inspection crops were made under /tmp. Source SHA-256 values are checked on reproduction. Preserve those source folders with the evidence files.

From the project root:

\x60\x60\x60bash
node thesis-documentation/evaluation/analyze-optical-latency.cjs .runtime/optical-latency/2026-10-22/readings-input.json /tmp/optical-latency-recheck
\x60\x60\x60

The script refuses to overwrite an existing output directory. It reproduces arithmetic from manual transcriptions; it does not perform OCR or independently validate the transcribed digits. The Word thesis has not been edited by this analysis.

## Tekst Per Punim

Vonesa optike u vleresua duke fotografuar ne te njejten pamje kronometrin referues dhe imazhin e tij ne transmetim. Nga 31 pamjet e transmetimit lokal dhe 28 pamjet e shikuesit ueb, u perdoren perkatesisht ${a.n} dhe ${b.n} cifte me shifra te lexueshme. Vonesa mesatare e vleresuar ishte rreth ${Math.round(a.mean_ms)} ms per transmetimin lokal dhe ${Math.round(b.mean_ms)} ms per shikuesin ueb; mediana ishte perkatesisht ${a.p50_ms} ms dhe ${b.p50_ms} ms, ndersa percentili i 95-te ishte ${a.p95_nearest_rank_ms} ms dhe ${b.p95_nearest_rank_ms} ms. Intervalet e vezhguara ishin ${a.min_ms}-${a.max_ms} ms dhe ${b.min_ms}-${b.max_ms} ms. Pamjet jane marre ne vazhdimesi dhe nuk trajtohen si prova te pavarura. Monitori referues punonte ne 60 Hz, me periudhe rifreskimi 16.67 ms. Shifrat dhjetore te kronometrit nuk nenkuptojne saktesi matjeje prej 1 ms; pasiguria e pergjithshme nuk u kuantifikua, pasi parametrat kohore te telefonit dhe ekranit marres nuk u percaktuan. Pamjet me shifra te mbivendosura u perjashtuan nga llogaritjet.
`;
}

function main() {
  const [inputPath,outputPath] = process.argv.slice(2);
  assert(inputPath && outputPath,'Usage: node analyze-optical-latency.cjs INPUT.json NEW_OUTPUT_DIRECTORY');
  assert(!fs.existsSync(outputPath),'Output directory already exists; choose a new one.');
  const input = JSON.parse(fs.readFileSync(inputPath,'utf8'));
  const result = analyze(input);
  const summary = {
    analysis_date:input.analysis_date, method:input.method, sampling:input.sampling,
    source_monitor_refresh_hz:input.source_monitor_refresh_hz,
    receiver_display_refresh_hz:input.receiver_display_refresh_hz,
    quantified_total_uncertainty_ms:null, independent_trials_claimed:false,
    transcription_input_sha256:crypto.createHash('sha256').update(fs.readFileSync(inputPath)).digest('hex'),
    sets:result.summaries
  };
  const adjacentSummary = path.join(path.dirname(inputPath),'summary.json');
  if (fs.existsSync(adjacentSummary)) {
    assert.deepEqual(summary,JSON.parse(fs.readFileSync(adjacentSummary,'utf8')),'Existing summary differs.');
    const previous = fs.readFileSync(path.join(path.dirname(inputPath),'readings.csv'),'utf8');
    for (const row of result.rows) assert(previous.includes(row.sha256),'Source image changed: '+row.source_path);
  }
  fs.mkdirSync(outputPath,{recursive:true});
  fs.copyFileSync(inputPath,path.join(outputPath,'readings-input.json'));
  const headers = Object.keys(result.rows[0]);
  fs.writeFileSync(path.join(outputPath,'readings.csv'),[headers.map(csvCell).join(','),...result.rows.map(r=>headers.map(h=>csvCell(r[h])).join(','))].join('\n')+'\n');
  fs.writeFileSync(path.join(outputPath,'summary.json'),JSON.stringify(summary,null,2)+'\n');
  fs.writeFileSync(path.join(outputPath,'OPTICAL_LATENCY_REPORT_2026-10-22.md'),report(input,result));
  for (const [id,s] of Object.entries(result.summaries)) console.log(id,JSON.stringify(s.descriptive_frame_statistics));
}

assert.equal(clock(95699),'00:01:35.699');
assert.equal(clock(10826),'00:00:10.826');
assert.equal(clock(null),'');
assert.deepEqual(stats([1,2,3,4]).p50_ms,2.5);
assert.equal(stats(Array.from({length:20},(_,i)=>i+1)).p95_nearest_rank_ms,19);
if (require.main === module) main();
module.exports = {clock,stats,analyze,report};
