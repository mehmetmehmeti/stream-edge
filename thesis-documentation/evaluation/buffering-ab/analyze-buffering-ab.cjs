'use strict';
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
function summary(values){
  const s=values.filter(Number.isFinite).sort((a,b)=>a-b),n=s.length;
  return {n,mean:n?s.reduce((a,b)=>a+b,0)/n:null,p50:n?(n%2?s[(n-1)/2]:(s[n/2-1]+s[n/2])/2):null,p95:n?s[Math.ceil(.95*n)-1]:null,min:n?s[0]:null,max:n?s[n-1]:null};
}
function analyzeProbe(rows){
  const start=rows.filter(r=>r.event==='measurement_start'),end=rows.filter(r=>r.event==='measurement_end');
  assert.equal(start.length,1,'Exactly one native start marker');assert.equal(end.length,1,'Exactly one native end marker');
  assert(end[0].mono_us>start[0].mono_us);assert(!rows.some(r=>r.event==='safety_abort'),'Safety guard aborted trial');
  const measured=rows.filter(r=>r.mono_us>=start[0].mono_us&&r.mono_us<end[0].mono_us);
  const out=measured.filter(r=>r.event==='queue_output'),encoded=measured.filter(r=>r.event==='encoded');
  assert(out.length>0&&encoded.length>0,'Frames must reach queue output and encoder');
  for(const row of [...out,...encoded])assert(row.matched&&row.entered_us>0&&row.mono_us>=row.entered_us,'PTS/time continuity');
  for(let i=1;i<out.length;i++)assert(out[i].id>out[i-1].id,'Queue output frame IDs must increase');
  const qs=measured.filter(r=>r.event==='queue_sample');
  const seconds=(end[0].mono_us-start[0].mono_us)/1e6;
  const skipped=out.at(-1).id-out[0].id+1-out.length;
  return {
    configuration:rows.find(r=>r.event==='configuration'),interval_s:seconds,
    start_mono_us:start[0].mono_us,end_mono_us:end[0].mono_us,
    start_utc_ms:start[0].utc_us/1000,end_utc_ms:end[0].utc_us/1000,
    queue_wait_ms:summary(out.map(r=>(r.mono_us-r.entered_us)/1000)),
    input_to_encoded_ms:summary(encoded.map(r=>(r.mono_us-r.entered_us)/1000)),
    queue_buffers:summary(qs.map(r=>r.buffers)),queue_bytes:summary(qs.map(r=>r.bytes)),
    publisher_rss_mib:summary(qs.map(r=>r.rss_bytes/1048576)),
    queue_output_fps:out.length/seconds,encoded_fps:encoded.length/seconds,
    intentionally_skipped_between_observed_output_ids:skipped,
    skipped_percent_between_observed_output_ids:100*skipped/(out.at(-1).id-out[0].id+1),
    input_counter_delta:end[0].input_count-start[0].input_count,
    output_counter_delta:end[0].output_count-start[0].output_count,
    notes:['Latency cohort is queue/encoder outputs during the native monotonic measurement window; entries may precede warmup end.',
      'Skipped IDs count only gaps between first and last observed queue-output IDs; startup, tail and upstream drops are not included.',
      'Queue wait and input-to-encoded time exclude capture, upstream bridge/SHM, RTP/network, receiver buffering and display.']
  };
}
function analyzeDirectory(directory){
  const result=JSON.parse(fs.readFileSync(path.join(directory,'result.json'),'utf8'));
  const trials=result.trials.filter(t=>t.status==='passed');
  const readJsonl=file=>fs.existsSync(file)?fs.readFileSync(file,'utf8').trim().split('\n').filter(Boolean).map(JSON.parse):[];
  const resources=readJsonl(path.join(directory,'resources.jsonl')),qt=readJsonl(path.join(directory,'qt.jsonl'));
  for(const t of trials){
    const rows=resources.filter(r=>r.utc_ms>=t.utc_start_ms&&r.utc_ms<=t.utc_end_ms&&r.valid_cpu_interval);
    t.resources={sample_count:rows.length,cpu_percent:summary(rows.map(r=>Object.values(r.components).reduce((s,c)=>s+c.cpu_percent,0))),rss_mib:summary(rows.map(r=>Object.values(r.components).reduce((s,c)=>s+c.rss_bytes,0)/1048576))};
    const frames=qt.filter(r=>r.utc_ms>=t.utc_start_ms&&r.utc_ms<=t.utc_end_ms&&r.live);
    t.qt=frames.length>=2?{interval_s:(frames.at(-1).elapsed_ns-frames[0].elapsed_ns)/1e9,rendered_frames:frames.at(-1).rendered_frames-frames[0].rendered_frames}:null;
    if(t.qt)t.qt.rendered_fps=t.qt.rendered_frames/t.qt.interval_s;
    t.browser_presentation_gap_ms=summary(t.browser_presentation_gaps_ms||[]);
  }
  const groups=[];
  for(const load of ['normal','slow'])for(const profile of ['fifo','latest']){
    const rows=trials.filter(t=>t.load===load&&t.profile===profile);
    if(!rows.length)continue;
    groups.push({load,profile,trials:rows.length,queue_p50_ms:summary(rows.map(t=>t.probe.queue_wait_ms.p50)),queue_p95_ms:summary(rows.map(t=>t.probe.queue_wait_ms.p95)),
      encoded_slice_p95_ms:summary(rows.map(t=>t.probe.input_to_encoded_ms.p95)),browser_fps:summary(rows.map(t=>t.web.presented_fps)),
      publisher_rss_mib:summary(rows.map(t=>t.probe.publisher_rss_mib.mean)),queue_buffers_max:Math.max(...rows.map(t=>t.probe.queue_buffers.max)),
      cpu_percent:summary(rows.map(t=>t.resources.cpu_percent.mean)),summed_rss_mib:summary(rows.map(t=>t.resources.rss_mib.mean)),qt_fps:summary(rows.map(t=>t.qt?.rendered_fps)),
      browser_gap_p95_ms:summary(rows.map(t=>t.browser_presentation_gap_ms.p95)),
      skipped_frames:rows.reduce((n,t)=>n+t.probe.intentionally_skipped_between_observed_output_ids,0),
      browser_freezes:rows.every(t=>Number.isFinite(t.browser_freeze_delta))?rows.reduce((n,t)=>n+t.browser_freeze_delta,0):null,
      recorder_queue_drops:rows.every(t=>Number.isFinite(t.recording.recorder_diagnostics.dropped_frames))?rows.reduce((n,t)=>n+t.recording.recorder_diagnostics.dropped_frames,0):null,
      files_verified:rows.filter(t=>t.recording.sha256_match).length});
  }
  const analysis={status:result.status,mode:result.mode,trial_count:trials.length,groups,
    optical_end_to_end_latency_measured:false,
    conclusion_scope:'Effect of one pre-encoder live queue policy in the existing shared-memory/WHIP pipeline on one laptop; all other bounded upstream stages and 16 MiB SHM pool unchanged.',
    safety:'FIFO queue limits disabled locally, but the upstream 16 MiB shared-memory pool can still impose backpressure. RSS growth is observed, not guaranteed unbounded.',
    aggregation:'Condition values summarize per-trial statistics, not a claim that individual frames are independent trials.'};
  analysis.trials=trials;
  fs.writeFileSync(path.join(directory,'analysis.json'),JSON.stringify(analysis,null,2)+'\n');
  const headers=['trial','load','profile','replicate','interval_s','queue_p50_ms','queue_p95_ms','input_to_encoded_p95_ms','browser_fps','queue_max_buffers','publisher_mean_rss_mib','skipped_frames','browser_freezes','recording_hash_match'];
  const csv=[headers.join(',')];
  for(const t of trials)csv.push([t.index,t.load,t.profile,t.replicate,t.probe.interval_s,t.probe.queue_wait_ms.p50,t.probe.queue_wait_ms.p95,t.probe.input_to_encoded_ms.p95,t.web.presented_fps,t.probe.queue_buffers.max,t.probe.publisher_rss_mib.mean,t.probe.intentionally_skipped_between_observed_output_ids,t.browser_freeze_delta??'',t.recording.sha256_match].join(','));
  fs.writeFileSync(path.join(directory,'comparison.csv'),csv.join('\n')+'\n');
  const fmt=n=>Number.isFinite(n)?n.toFixed(2):'not measured';
  let md=`# Buffering A/B Evaluation\n\nRun: ${result.run_id}. Mode: ${result.mode}. Status: ${result.status}.\n\n${analysis.conclusion_scope}\n\n| Load | Policy | Trials | Mean trial p50 queue wait (ms) | Mean trial p95 queue wait (ms) | Mean browser FPS | Maximum queue buffers | Mean publisher RSS (MiB) | Skipped live frames |\n|---|---|---:|---:|---:|---:|---:|---:|---:|\n`;
  for(const g of groups)md+=`| ${g.load} | ${g.profile} | ${g.trials} | ${fmt(g.queue_p50_ms.mean)} | ${fmt(g.queue_p95_ms.mean)} | ${fmt(g.browser_fps.mean)} | ${g.queue_buffers_max} | ${fmt(g.publisher_rss_mib.mean)} | ${g.skipped_frames} |\n`;
  md+=`\n## Resource And Playback Checks\n\n| Load | Policy | Qt FPS | CPU % (one core = 100%) | Summed RSS MiB | Browser gap p95 ms (mean of trial p95s) | Browser freeze count | Recorder queue drops | Verified recordings |\n|---|---|---:|---:|---:|---:|---:|---:|---:|\n`;
  for(const g of groups)md+=`| ${g.load} | ${g.profile} | ${fmt(g.qt_fps.mean)} | ${fmt(g.cpu_percent.mean)} | ${fmt(g.summed_rss_mib.mean)} | ${fmt(g.browser_gap_p95_ms.mean)} | ${g.browser_freezes??'not measured'} | ${g.recorder_queue_drops??'not measured'} | ${g.files_verified}/${g.trials} |\n`;
  md+=`\n## Boundaries\n\nThese are monotonic queue/publisher slice timings, not optical camera-to-screen latency. Synthetic validation and pilot runs are separate from measured camera trials. The artificial slowdown is 100 ms per raw frame after the tested queue; it is not a claim that this slowdown naturally occurs in deployment. Camera mode is F450 1280 x 704 YUYV, nominal 15 FPS; VP8 live encoder settings remain fixed. Recording stays enabled in every trial.\n\nOne laptop holds hardware and network topology fixed while isolating buffering. This measures the actual web viewer through WHIP/mediasoup/WebRTC, not an inference from the Qt preview. It does not establish identical performance on two hosts.\n\n${analysis.safety}\n\nQueue outputs during the measurement window form the latency cohort. Frame gaps identify queue-stage discards only; pending tail and upstream losses are not counted as zero. CPU/RSS samples and browser counters are retained separately. MP4 hashes and decoded frame counts test recording integrity, not loss-free network playback. Only the measured data may establish an improvement; completing the protocol alone does not prove one.\n`;
  for(const load of ['normal','slow']){
    const a=groups.find(g=>g.load===load&&g.profile==='fifo'),b=groups.find(g=>g.load===load&&g.profile==='latest');
    if(a&&b&&a.queue_p95_ms.mean>0)md+=`\n${load}: mean per-trial p95 queue wait changed from ${fmt(a.queue_p95_ms.mean)} to ${fmt(b.queue_p95_ms.mean)} ms (${fmt(100*(a.queue_p95_ms.mean-b.queue_p95_ms.mean)/a.queue_p95_ms.mean)}% reduction; a negative value means an increase). Interpret alongside FPS, freezes, skipped frames and recording checks.\n`;
  }
  fs.writeFileSync(path.join(directory,'BUFFERING_AB_REPORT.md'),md);
  return analysis;
}
if(require.main===module){if(process.argv[2])console.log(JSON.stringify(analyzeDirectory(process.argv[2]),null,2));else console.log('Usage: node analyze-buffering-ab.cjs RUN_DIRECTORY');}
module.exports={summary,analyzeProbe,analyzeDirectory};
