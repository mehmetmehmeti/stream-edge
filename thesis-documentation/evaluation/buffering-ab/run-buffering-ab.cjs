'use strict';
const fs=require('node:fs');
const path=require('node:path');
const cp=require('node:child_process');
const crypto=require('node:crypto');
const assert=require('node:assert/strict');
const args=process.argv.slice(2);
const option=(name,fallback)=>args.includes(name)?args[args.indexOf(name)+1]:fallback;
const root=option('--root','<PROJECT_ROOT>');
const pilot=args.includes('--pilot');
const runId=new Date().toISOString().replace(/[:.]/g,'-');
const output=option('--output',path.join(root,'.runtime/buffering-ab',runId));
const warmup=pilot?3:15,sampleSeconds=pilot?6:60,repeats=pilot?1:3;
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const directory=__dirname;
const {installBrowserProbe,startPresentedCounter,browserSnapshot,lockMeasurementInput}=require(path.join(root,'thesis-documentation/evaluation/browser-probe.cjs'));
const {webMetrics}=require(path.join(root,'thesis-documentation/evaluation/metrics.cjs'));
const {analyzeProbe,analyzeDirectory}=require(path.join(directory,'analyze-buffering-ab.cjs'));
const tokens={},children=[],services=[];
const webApi='http://127.0.0.1:3000',edgeApi='http://127.0.0.1:3002',control='http://127.0.0.1:8081',webUrl='http://localhost:3001';
let pgStarted=false,browserServer,browser,context,page,streamId,recordingId,cleaning=false;
let credentials;
const report={schema_version:1,run_id:runId,mode:pilot?'pilot':'measured',status:'preparing',trials:[],commands:[],protocol:{warmup_s:warmup,sample_s:sampleSeconds,replicates_per_policy_per_load:repeats,
  camera:'Intel F450, 1280x704 YUYV, nominal 15 FPS',recording:'Enabled throughout warmup and measurement for every trial',
  fifo:'Only the named pre-encoder queue has all size limits disabled and leaky=no; other upstream bounds stay unchanged',latest:'Same queue max-size-buffers=2, bytes/time limits disabled, leaky=downstream',slowdown_us:100000,
  topology:'One laptop, Qt preview plus actual browser viewer through local mediasoup/WHIP/WebRTC',
  single_laptop_reason:'Hold host and network topology fixed while isolating the pre-encoder buffering policy. Actual web playback is measured, not inferred from Qt. Results are not a two-host optical latency comparison.',
  primary_latency:'Local monotonic queue-entry to queue-output waiting time; publisher slice to encoded output is separate',optical_latency:'Not measured in this A/B runner; earlier photos are separate observations, not the A/B baseline',
  input_to_output_clock:'GLib monotonic microseconds in the publisher process',cpu:'100 percent = one logical core; summed RSS can double-count shared memory',memory_guard:'Abort if publisher RSS exceeds 768 MiB or tested queue exceeds 300 buffers',safety:'No DB resets, queue deletion, existing recording deletion, or persistent env edits'}};
function save(){fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(report,null,2)+'\n',{mode:0o600});}
function log(s){console.log(`[${new Date().toISOString()}] ${s}`);}
function jsonl(file){if(!fs.existsSync(file))return [];const raw=fs.readFileSync(file,'utf8');return raw.slice(0,raw.lastIndexOf('\n')+1).split('\n').filter(Boolean).map(JSON.parse);}
async function until(label,fn,seconds=90){const end=performance.now()+seconds*1000;while(performance.now()<end){const result=await fn();if(result)return result;await pause(200);}throw Error('Timeout: '+label);}
async function api(base,route,method='GET',body){const r=await fetch(base+route,{method,headers:{'Content-Type':'application/json',...(tokens[base]?{Authorization:'Bearer '+tokens[base]}:{})},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(90000)});const d=await r.json();if(!r.ok)throw Error(`${method} ${route}: HTTP ${r.status}`);return d;}
async function action(route,body={}){
  const a=await api(webApi,route,'POST',body);
  if(route==='/streams/start')streamId=a.stream.id;
  if(route.endsWith('/recordings/start'))recordingId=a.video.id;
  const copies=await until('command completed on both replicas',async()=>{
    const values=await Promise.all([webApi,edgeApi].map(async base=>(await api(base,'/commands')).commands.find(c=>c.id===a.command_id)));
    if(values.some(c=>c?.status==='failed'))throw Error('Application command failed: '+a.command_id);
    return values.every(c=>c?.status==='completed')?values:null;
  });
  assert.equal(copies[0].version,copies[1].version);
  report.commands.push({id:a.command_id,route,status:'completed',version:copies[0].version});save();return a;
}
function child(command,argv,cwd,env,filename,service=false){
  const fd=fs.openSync(path.join(output,filename),'a',0o600);
  const p=cp.spawn(command,argv,{cwd,env:{...process.env,...env},stdio:['ignore',fd,fd]});fs.closeSync(fd);
  p.on('error',e=>{report.child_error=e.message;});(service?services:children).push(p);return p;
}
async function stopChild(p){if(p.exitCode!==null||p.signalCode!==null)return;p.kill('SIGTERM');await Promise.race([new Promise(r=>p.once('exit',r)),pause(5000)]);if(p.exitCode===null&&p.signalCode===null){p.kill('SIGKILL');await Promise.race([new Promise(r=>p.once('exit',r)),pause(2000)]);}}
async function startServices(){
  for(const port of [3000,3001,3002,3010,8081]){
    const listeners=cp.execFileSync('ss',['-H','-ltn',`sport = :${port}`],{encoding:'utf8'}).trim();assert(!listeners,'Port already occupied: '+port);
  }
  const pgdata=path.join(root,'.runtime/postgres-stream-edge');
  assert(fs.existsSync(path.join(pgdata,'PG_VERSION')),'Existing project DB required; no new cluster is initialized');
  try{cp.execFileSync('/usr/lib/postgresql/16/bin/pg_ctl',['-D',pgdata,'status'],{stdio:'ignore'});}catch{
    cp.execFileSync('/usr/lib/postgresql/16/bin/pg_ctl',['-D',pgdata,'-l',path.join(output,'postgres.log'),'-w','start'],{stdio:'ignore'});pgStarted=true;
  }
  const common={CORS_ALLOWED_ORIGINS:'http://localhost:3001,http://127.0.0.1:3001',MEDIA_SERVER_PUBLIC_URL:'http://127.0.0.1:3010',MEDIA_SERVER_API_URL:'http://127.0.0.1:3010',PUBLIC_API_URL:webApi};
  child(process.execPath,['server.mjs'],path.join(root,'media-server'),{...common,HTTP_PORT:'3010',ANNOUNCED_ADDRESS:'127.0.0.1'},'media.log',true);
  child(process.execPath,['dist/src/main.js'],path.join(root,'backend-web'),{...common,CLOUD_RECORDINGS_DIR:path.join(root,'backend-web/recordings')},'backend-web.log',true);
  child(process.execPath,['dist/src/main.js'],path.join(root,'backend-edge'),{...common,CLOUD_API_URL:webApi,CLOUD_SYNC_URL:'ws://127.0.0.1:3000/sync',THESIS_TRANSFER_TIMING_FILE:path.join(output,'transfers.jsonl')},'backend-edge.log',true);
  const template=`${process.execPath} ${path.join(directory,'launch-ab-publisher.cjs')} --url '{whip_url}' --token '{publish_token}' --socket '{shm_socket_path}' --format '{gst_video_format}' --width '{frame_width}' --height '{frame_height}' --fps '{frame_rate_num}' --keyframe '{keyframe_interval}'`;
  const streamEnv={...common,BACKEND_API_BASE_URL:edgeApi,MEDIA_PUBLISH_WHIP_URL:'http://127.0.0.1:3010/whip',MEDIA_PUBLISH_VIEW_URL:webUrl,MEDIA_PUBLISH_COMMAND_TEMPLATE:template,
    THESIS_AB_CONTROL:path.join(output,'publisher-control.json'),STREAMING_RECORD_OUTPUT_DIR:path.join(root,'stream-edge/streaming-service/recordings'),
    STREAMING_CAMERA_WIDTH:'1280',STREAMING_CAMERA_HEIGHT:'704',STREAMING_CAMERA_FRAME_RATE:'15',STREAMING_CAMERA_PIXEL_FORMAT:'YUYV',STREAMING_RECORD_WIDTH:'1280',STREAMING_RECORD_HEIGHT:'704',STREAMING_RECORD_FPS:'15',CLOUD_PUBLISH_FRAME_RATE_NUM:'15',CLOUD_PUBLISH_FRAME_RATE_DEN:'1',CLOUD_PUBLISH_SHM_SIZE_BYTES:'16777216',CLOUD_PUBLISH_SHM_SOCKET_PATH:`/tmp/thesis-ab-${runId}.sock`,EDGE_COMMAND_SYNC_ENABLED:'0'};
  child(path.join(root,'stream-edge/streaming-service/build/streaming_service'),[],path.join(root,'stream-edge/streaming-service'),streamEnv,'stream.log',true);
  child(process.execPath,['node_modules/next/dist/bin/next','dev','--port','3001'],path.join(root,'frontend-web'),{...common,NEXT_PUBLIC_API_URL:webApi,NEXT_PUBLIC_MEDIA_SERVER_URL:'http://127.0.0.1:3010'},'frontend-web.log',true);
  for(const url of [webApi+'/auth/me',edgeApi+'/auth/me',control+'/stream/status','http://127.0.0.1:3010/api/health',webUrl])await until('service '+url,async()=>{try{await fetch(url,{signal:AbortSignal.timeout(1000)});return true;}catch{return false;}},100);
  report.processes=services.map(p=>({pid:p.pid}));save();
}
async function authenticate(){
  credentials={email:`buffering-${runId}@local.test`,password:crypto.randomBytes(18).toString('hex')};
  const a=await api(webApi,'/auth/register','POST',{...credentials,fullName:'Buffering A/B evaluation',role:'OPERATOR'});tokens[webApi]=a.token;
  await until('test account replicated',async()=>{try{tokens[edgeApi]=(await api(edgeApi,'/auth/login','POST',credentials)).token;return true;}catch{return false;}});
  const streams=(await api(webApi,'/streams')).streams;assert(!streams.some(s=>s.device_id==='edge-dev-001'&&['live','starting','stopping'].includes(s.status)),'Existing active device stream; no reset performed');
  const {chromium}=require(path.join(root,'thesis-documentation/evaluation/node_modules/playwright'));
  browserServer=await chromium.launchServer({executablePath:'/usr/bin/google-chrome',headless:false,args:['--disable-background-timer-throttling','--disable-renderer-backgrounding']});
  browser=await chromium.connect(browserServer.wsEndpoint());context=await browser.newContext({viewport:{width:1360,height:900}});
  await context.addInitScript(installBrowserProbe,{});
  page=await context.newPage();page.on('dialog',d=>d.dismiss());await page.goto(webUrl,{waitUntil:'domcontentloaded'});
  await page.getByLabel('Email',{exact:true}).fill(credentials.email);await page.getByLabel('Password',{exact:true}).fill(credentials.password);
  await page.getByRole('button',{name:'Enter Console',exact:true}).click();await page.getByRole('button',{name:'Sign out',exact:true}).waitFor({timeout:60000});
  child('python3',[path.join(root,'thesis-documentation/evaluation/sample_resources.py'),'--root',root,'--out',path.join(output,'resources.jsonl'),'--browser-pid',String(browserServer.process().pid)],root,{},'resources.log');
  child(path.join(root,'frontend-edge/build-local/appDiplomaFrontendEdge'),['--benchmark-camera'],path.join(root,'frontend-edge'),{DIPLOMA_TEST_API_URL:edgeApi,DIPLOMA_TEST_AUTH_TOKEN:tokens[edgeApi],DIPLOMA_TEST_OUTPUT:path.join(output,'qt.jsonl'),XDG_CONFIG_HOME:path.join(output,'private-qt-settings')},'qt.log');
}
async function verifyRecording(id){
  const copies=await until('recording copied and synchronized',async()=>{const v=await Promise.all([webApi,edgeApi].map(base=>api(base,`/videos/${id}`)));return v.every(x=>x.video.status==='completed')?v.map(x=>x.video):null;},180);
  const v=copies[0];assert.equal(v.frame_count,copies[1].frame_count);assert.equal(v.size_bytes,copies[1].size_bytes);
  const hash=async file=>{const h=crypto.createHash('sha256');for await(const b of fs.createReadStream(file))h.update(b);return h.digest('hex');};
  const a=await hash(v.edge_local_path),b=await hash(v.cloud_path);assert.equal(a,b,'Recording hash mismatch');
  const probe=file=>JSON.parse(cp.execFileSync('ffprobe',['-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=codec_name,width,height,nb_read_frames:format=duration,size','-of','json',file],{encoding:'utf8',timeout:90000}));
  const local=probe(v.edge_local_path),remote=probe(v.cloud_path);assert.equal(local.streams[0].nb_read_frames,remote.streams[0].nb_read_frames);assert.equal(String(v.frame_count),remote.streams[0].nb_read_frames);
  return {id,sha256_match:true,sha256:a,bytes:fs.statSync(v.cloud_path).size,local_path:v.edge_local_path,server_path:v.cloud_path,probe:remote};
}
async function trial(load,profile,replicate){
  const t={index:report.trials.length+1,load,profile,replicate,status:'running',started_at:new Date().toISOString()};report.trials.push(t);save();
  const name=`${String(t.index).padStart(2,'0')}-${load}-${profile}`,base=path.join(output,name);fs.mkdirSync(base,{mode:0o700});
  const config={profile,sleep_us:load==='slow'?100000:0,binary:option('--publisher-binary',path.join(directory,'ab-build/ab-whip-client')),log_path:path.join(base,'probe.jsonl'),pipeline_path:path.join(base,'pipeline.json'),pid_path:path.join(base,'publisher.pid')};
  fs.writeFileSync(path.join(output,'publisher-control.json'),JSON.stringify(config,null,2)+'\n',{mode:0o600});
  log(`Trial ${t.index}: ${load}/${profile}, repetition ${replicate}`);
  await action('/streams/start',{device_id:'edge-dev-001',title:`Buffering ${runId} ${name}`});t.stream_id=streamId;
  await page.close();page=await context.newPage();page.on('dialog',d=>d.dismiss());await page.goto(webUrl,{waitUntil:'domcontentloaded'});
  await page.waitForFunction(()=>{const v=document.querySelector('.live-viewer video');return v?.videoWidth>0&&v.getVideoPlaybackQuality().totalVideoFrames>10;},null,{timeout:90000});
  await page.evaluate(startPresentedCounter);await page.evaluate(lockMeasurementInput);
  const first=await page.evaluate(browserSnapshot);assert.equal(first.width,1280);assert.equal(first.height,704);
  await until('Qt frames',async()=>{const rows=jsonl(path.join(output,'qt.jsonl'));const last=rows.at(-1);return last?.live&&last.rendered_frames>10;});
  const start=await action(`/streams/${streamId}/recordings/start`);t.video_id=start.video.id;
  await until('recorder active',async()=>(await api(control,'/record/status')).recordingStatus.active);
  await until('native probe configuration',async()=>jsonl(config.log_path).some(r=>r.event==='configuration'));
  const pid=Number(fs.readFileSync(config.pid_path,'utf8').trim());assert(Number.isSafeInteger(pid)&&pid>1);
  log(`Trial ${t.index}: ${warmup}s warmup, then ${sampleSeconds}s measurement`);await pause(warmup*1000);
  await page.evaluate(()=>{window.__abFrameTimes=[];const v=document.querySelector('.live-viewer video');const next=(now)=>{if(window.__abFrameTimes)window.__abFrameTimes.push(now);v.requestVideoFrameCallback(next);};v.requestVideoFrameCallback(next);});
  process.kill(pid,'SIGUSR1');await until('native measurement start',async()=>jsonl(config.log_path).find(r=>r.event==='measurement_start'),5);
  const samples=[],started=performance.now();
  do{
    const web=await page.evaluate(browserSnapshot),status=await api(control,'/stream/status');
    assert(status.streaming&&status.recordingStatus.active,'Stream/recording stopped');assert.equal(web.visible,'visible');assert(web.pixel_range>=5,'Nonuniform video check');
    samples.push(web);fs.appendFileSync(path.join(base,'samples.jsonl'),JSON.stringify({web,status})+'\n');
    if(samples.length%15===0)log(`Trial ${t.index}: ${Math.round((performance.now()-started)/1000)}/${sampleSeconds}s`);
    assert(!jsonl(config.log_path).some(r=>r.event==='safety_abort'),'Publisher safety abort');
    await pause(Math.max(0,Math.min(1000,sampleSeconds*1000-(performance.now()-started))));
  }while(performance.now()-started<sampleSeconds*1000);
  samples.push(await page.evaluate(browserSnapshot));
  process.kill(pid,'SIGUSR2');await until('native measurement end',async()=>jsonl(config.log_path).some(r=>r.event==='measurement_end'),5);
  t.utc_start_ms=samples[0].utc_ms;t.utc_end_ms=samples.at(-1).utc_ms;
  t.probe=analyzeProbe(jsonl(config.log_path));t.web=webMetrics(samples[0],samples.at(-1));assert(t.web.rtp_counter_continuity&&t.web.presented_fps>0);
  assert(t.probe.interval_s>=sampleSeconds-.2&&t.probe.interval_s<sampleSeconds+5,'Unexpected native measurement duration');
  assert.equal(t.probe.configuration.leaky,profile==='latest'?2:0);assert.equal(t.probe.configuration.max_buffers,profile==='latest'?2:0);assert.equal(t.probe.configuration.consumer_sleep_us,config.sleep_us);
  if(profile==='latest')assert(t.probe.queue_buffers.max<=2);else assert.equal(t.probe.intentionally_skipped_between_observed_output_ids,0);
  const a=samples[0].rtp,b=samples.at(-1).rtp;t.browser_freeze_delta=Number.isFinite(a.freezeCount)&&Number.isFinite(b.freezeCount)?b.freezeCount-a.freezeCount:null;
  t.browser_freeze_duration_s_delta=Number.isFinite(a.totalFreezesDuration)&&Number.isFinite(b.totalFreezesDuration)?b.totalFreezesDuration-a.totalFreezesDuration:null;
  const times=await page.evaluate(()=>window.__abFrameTimes);t.browser_presentation_gaps_ms=times.slice(1).map((x,i)=>x-times[i]);
  await page.screenshot({path:path.join(base,'viewer.png'),fullPage:true});
  await action(`/streams/${streamId}/recordings/stop`,{video_id:recordingId});const id=recordingId;recordingId=null;
  const diagnostics=(await api(control,'/record/status')).recordingStatus;
  assert(diagnostics.finalized,'Recorder finalization missing');
  assert(Number.isFinite(diagnostics.dropped_frames),'Missing recorder drop counter');
  assert.equal(diagnostics.dropped_frames,0,'Recorder queue drops occurred');
  assert.equal(diagnostics.received_frames,diagnostics.written_frames,'Recorder received/written frame mismatch');
  t.recording=await verifyRecording(id);t.recording.recorder_diagnostics=diagnostics;
  await action(`/streams/${streamId}/stop`);streamId=null;
  t.status='passed';t.finished_at=new Date().toISOString();t.probe_log=config.log_path;save();
  log(`Trial ${t.index} passed: queue p95 ${t.probe.queue_wait_ms.p95.toFixed(2)} ms; browser ${t.web.presented_fps.toFixed(2)} FPS; MP4 matched`);
}
async function cleanup(){
  if(cleaning)return;cleaning=true;
  if(recordingId&&streamId)await action(`/streams/${streamId}/recordings/stop`,{video_id:recordingId}).catch(e=>report.cleanup_error=e.message);
  if(streamId)await action(`/streams/${streamId}/stop`).catch(e=>report.cleanup_error=e.message);
  for(const p of children.reverse())await stopChild(p);
  await browser?.close().catch(()=>{});await browserServer?.close().catch(()=>{});
  for(const p of services.reverse())await stopChild(p);
  if(pgStarted)try{cp.execFileSync('/usr/lib/postgresql/16/bin/pg_ctl',['-D',path.join(root,'.runtime/postgres-stream-edge'),'-m','fast','-w','stop'],{stdio:'ignore'});}catch{report.database_cleanup_error='PostgreSQL stop failed';}
}
async function main(){
  assert(args.includes('--camera'),'Explicit --camera required');assert(!fs.existsSync(output),'Choose new output directory');fs.mkdirSync(output,{recursive:true,mode:0o700});save();
  await startServices();log('Self-contained services started; persistent environment unchanged');
  cp.execFileSync('python3',[path.join(root,'thesis-documentation/evaluation/collect_hardware.py'),'--root',root,'--out',path.join(output,'hardware.json')],{stdio:'ignore'});
  const hardware=JSON.parse(fs.readFileSync(path.join(output,'hardware.json')));assert(/F450/.test(hardware.camera_settings?.stdout||''),'Expected Intel F450');report.hardware_model=hardware.camera_settings.stdout.match(/Card type\s*:\s*(.+)/)?.[1]?.trim();
  report.source_hashes=Object.fromEntries(['run-buffering-ab.cjs','analyze-buffering-ab.cjs','launch-ab-publisher.cjs','ab-probe.c','ab-probe.h'].map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(directory,n))).digest('hex')]));
  await authenticate();report.status='running';save();
  for(const load of ['normal','slow'])for(let r=1;r<=repeats;r++)for(const profile of ((r+(load==='slow'?1:0))%2?['fifo','latest']:['latest','fifo']))await trial(load,profile,r);
  report.status='passed';report.finished_at=new Date().toISOString();save();
}
if(args.includes('--help')||!args.length){console.log('Usage: node run-buffering-ab.cjs --camera [--pilot] [--publisher-binary PATH] [--output NEW_DIRECTORY]\nRuns a self-contained real F450 camera experiment; creates test recordings/accounts, preserves existing data, and stops only its own services.');}
else{
  for(const signal of ['SIGINT','SIGTERM'])process.once(signal,()=>{report.status='interrupted';report.error=signal;cleanup().finally(()=>{save();process.exit(130);});});
  main().catch(async e=>{report.status='failed';report.error=e.message;const t=report.trials.at(-1);if(t?.status==='running'){t.status='failed';t.error=e.message;}console.error(e.stack);if(page&&!page.isClosed())await page.screenshot({path:path.join(output,'failure.png'),fullPage:true}).catch(()=>{});process.exitCode=1;}).finally(async()=>{await cleanup();if(fs.existsSync(output)){save();analyzeDirectory(output);}log('Final status: '+report.status+'; evidence '+output);});
}
