'use strict';
const assert=require('node:assert/strict');
const {summary,analyzeProbe}=require(process.argv[2]||'./analyze-buffering-ab.cjs');
assert.equal(summary([1,2,3,4]).p50,2.5);
assert.equal(summary(Array.from({length:20},(_,i)=>i+1)).p95,19);
assert.equal(summary([]).p95,null);
const row=(event,mono_us,id,entered_us)=>({event,mono_us,id,entered_us,matched:true});
const rows=[
  {event:'configuration',max_buffers:2,leaky:2},
  row('input',100,1,100),
  {event:'measurement_start',mono_us:200,utc_us:1000200,input_count:1,output_count:0,encoded_count:0},
  row('queue_output',300,1,100),row('encoded',350,1,100),
  row('input',400,2,400),row('input',500,3,500),
  row('queue_output',600,3,500),row('encoded',650,3,500),
  row('input',700,4,700),
  {event:'queue_sample',mono_us:710,buffers:1,bytes:100,queue_time_ns:1,rss_bytes:1048576},
  {event:'measurement_end',mono_us:800,utc_us:1000800,input_count:4,output_count:2,encoded_count:2},
  row('queue_output',900,4,700),row('encoded',950,4,700)
];
const result=analyzeProbe(rows);
assert.equal(result.queue_wait_ms.n,2);
assert(Math.abs(result.queue_wait_ms.p50-.15)<1e-12);
assert.equal(result.queue_wait_ms.p95,.2);
assert.equal(result.intentionally_skipped_between_observed_output_ids,1);
assert.equal(result.publisher_rss_mib.mean,1);
assert.equal(result.input_counter_delta,3);
assert.equal(result.output_counter_delta,2);
assert.throws(()=>analyzeProbe([...rows,{event:'safety_abort',mono_us:799}]));
assert.throws(()=>analyzeProbe(rows.map(r=>r.event==='encoded'?{...r,matched:false}:r)));
assert.throws(()=>analyzeProbe(rows.filter(r=>r.event!=='measurement_end')));
assert.throws(()=>analyzeProbe(rows.map(r=>r.event==='queue_output'&&r.id===3?{...r,id:1}:r)));
console.log('PASS: median/p95, empty counters, warmup-boundary cohort, pending-tail exclusion, true skips, missing markers, safety abort, unmatched PTS and reordering.');
