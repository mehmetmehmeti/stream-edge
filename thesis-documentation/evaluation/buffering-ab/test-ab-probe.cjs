'use strict';
const fs=require('node:fs');const cp=require('node:child_process');const assert=require('node:assert/strict');
const binary=process.argv[2],directory=process.argv[3];
fs.mkdirSync(directory,{recursive:true});
const median=values=>{const s=[...values].sort((a,b)=>a-b);return s[Math.floor(s.length/2)];};
async function test(profile,slow){
  const label=profile+'-'+slow,path=directory+'/'+label+'.jsonl';
  const queue=profile==='fifo'?'max-size-buffers=0 max-size-bytes=0 max-size-time=0 leaky=no':'max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream';
  const pipeline=`videotestsrc is-live=true num-buffers=30 ! video/x-raw,width=320,height=240,framerate=15/1 ! queue name=ab_queue ${queue} ! identity name=ab_delay sleep-time=${slow} ! videoconvert ! vp8enc name=ab_encoder deadline=1 cpu-used=8 ! fakesink sync=false`;
  const child=cp.spawn(binary,[pipeline],{env:{...process.env,THESIS_AB_LOG:path},stdio:['ignore','pipe','pipe']});
  let errors='';child.stderr.on('data',b=>errors+=b);
  const start=setTimeout(()=>child.kill('SIGUSR1'),400),end=setTimeout(()=>child.kill('SIGUSR2'),1600);
  const code=await new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',resolve);});
  clearTimeout(start);clearTimeout(end);assert.equal(code,0,errors);
  const rows=fs.readFileSync(path,'utf8').trim().split('\n').map(JSON.parse);
  assert.equal(rows.filter(r=>r.event==='input').length,30);
  assert.equal(rows.filter(r=>r.event==='measurement_start').length,1);
  assert.equal(rows.filter(r=>r.event==='measurement_end').length,1);
  const out=rows.filter(r=>r.event==='queue_output');const encoded=rows.filter(r=>r.event==='encoded');
  assert(out.every(r=>r.matched));assert(encoded.every(r=>r.matched));assert.equal(out.length,encoded.length);
  if(profile==='fifo'||slow===0)assert.equal(out.length,30);else assert(out.length<30);
  if(profile==='latest')assert(rows.filter(r=>r.event==='queue_sample').every(r=>r.buffers<=2));
  const result={profile,slow,input:30,output:out.length,median_queue_wait_ms:median(out.map(r=>(r.mono_us-r.entered_us)/1000))};
  console.log(JSON.stringify(result));return result;
}
(async()=>{
  const results=[];
  for(const slow of [0,100000])for(const profile of ['fifo','latest'])results.push(await test(profile,slow));
  assert(results.find(r=>r.profile==='fifo'&&r.slow).median_queue_wait_ms>results.find(r=>r.profile==='latest'&&r.slow).median_queue_wait_ms);
  fs.writeFileSync(directory+'/test-results.json',JSON.stringify({input_kind:'synthetic videotestsrc; instrumentation validation only, not camera evidence',status:'passed',results},null,2)+'\n');
  console.log('PASS: queue policies, PTS matching through VP8, phase markers and synthetic slowdown.');
})().catch(e=>{console.error(e);process.exitCode=1;});
