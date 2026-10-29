const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const cp = require('node:child_process');
const { createHash } = require('node:crypto');
const { Readable } = require('node:stream');
const { pipeline } = require('node:stream/promises');

const args = process.argv.slice(2);
const option = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const root = path.resolve(option('--root', path.join(__dirname, '../..')));
const directory = path.join(root, 'thesis-documentation/evaluation');
const { summary, webMetrics } = require(path.join(directory, 'metrics.cjs'));
const { installBrowserProbe, startPresentedCounter, browserSnapshot } = require(path.join(directory, 'browser-probe.cjs'));
const dotenv = require(path.join(root, 'backend-edge/node_modules/dotenv'));
const env = dotenv.parse(fs.readFileSync(path.join(root, 'backend-edge/.env')));
const streamEnv = dotenv.parse(fs.readFileSync(path.join(root, 'stream-edge/streaming-service/.env')));
const serverApi = option('--server-api', 'http://192.168.3.162:3000');
const webUrl = option('--web-url', 'http://192.168.3.162:3001');
const mediaUrl = option('--media-url', 'http://192.168.3.162:3010');
const edgeApi = 'http://127.0.0.1:3002';
const control = 'http://127.0.0.1:8081';
const deviceId = env.EDGE_DEVICE_ID || 'edge-dev-001';
const mode = option('--mode', 'short');
const cycles = mode === 'cycles' ? Number(option('--cycles', '30')) : 1;
const recordSeconds = mode === 'cycles' ? 5 : 20;
const runId = new Date().toISOString().replace(/[:.]/g, '-');
const output = path.resolve(option('--output', path.join(root, '.runtime/lan-evaluation', runId)));
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const report = { schema_version: 1, run_id: runId, host: os.hostname(), started_at: new Date().toISOString(),
  mode, requested_cycles: cycles, status: 'preparing', cycles: [], commands: [], setup_commands: [],
  protocol: { server_api: serverApi, web_url: webUrl, media_url: mediaUrl, edge_api: edgeApi,
    recording_seconds_minimum: recordSeconds, command_poll_pause_ms: 200, frame_poll_pause_ms: 100,
    L_command: 'Client monotonic POST initiation to first observation of COMPLETED on BOTH replicas; includes request, execution, replication and polling.',
    L_startup: 'Client monotonic POST initiation to first observation of a decoded frame in this laptop diagnostic browser; includes normal dashboard polling. Not optical latency.',
    timestamp_differences: 'No edge/server clock subtraction is used for durations.',
    file_check: 'Hash and ffprobe local original versus authenticated MP4 bytes served by the remote API. Independent on-server filesystem evidence must be collected on the server.',
    viewing: 'Qt plus diagnostic browser on edge laptop. Any additional browser on the server is an additional viewer; media health counts are sampled.',
    cpu: 'Local processes only; 100 percent means one logical core. Remote resource sampling requires the server session.',
    optical_latency: 'Not measured here; timer photographs are a separate experiment.' } };
let token, browserServer, browser, page, resourceChild, currentStream, activeVideo;
let localDb, user, abortRequested = false;
const json = value => JSON.stringify(value, null, 2) + '\n';
function save() { fs.writeFileSync(path.join(output, 'result.json'), json(report), { mode: 0o600 }); }
function event(type, detail) {
  fs.appendFileSync(path.join(output, 'events.jsonl'), JSON.stringify({ utc: new Date().toISOString(), monotonic_ms: performance.now(), type, ...detail }) + '\n');
}
function log(message) { console.log(`[${new Date().toISOString()}] ${message}`); }
async function api(base, route, method = 'GET', body) {
  const response = await fetch(base + route, { method,
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(15000) });
  const payload = await response.json();
  if (!response.ok) throw new Error(`${method} ${route}: HTTP ${response.status}: ${JSON.stringify(payload)}`);
  return payload;
}
async function until(label, check, timeout = 90000, delay = 200) {
  const start = performance.now();
  while (performance.now() - start < timeout) {
    if (abortRequested) throw new Error('Run interrupted');
    const value = await check(); if (value) return value;
    await pause(delay);
  }
  throw new Error(`Timeout: ${label}`);
}
async function copies(route, key) {
  const web = await api(serverApi, route);
  const edge = await api(edgeApi, route);
  return { web: web[key], edge: edge[key] };
}
async function complete(id, started, route, setup = false) {
  const history = [], firstSeen = {}, lastPending = { web: null, edge: null };
  const values = await until('command ' + id, async () => {
    const values = {};
    for (const side of ['web', 'edge']) {
      const payload = await api(side === 'web' ? serverApi : edgeApi, '/commands');
      values[side] = payload.commands.find(c => c.id === id);
    }
    const ms = performance.now() - started;
    for (const side of ['web', 'edge']) {
      const c = values[side];
      if (!c) continue;
      if (c.status === 'failed') throw new Error(c.error_message || 'Command failed');
      const h = history.filter(x => x.side === side).at(-1);
      if (h?.status !== c.status || h?.version !== c.version) history.push({ side, observed_ms: ms, status: c.status, version: c.version });
      if (c.status === 'completed' && firstSeen[side] === undefined) firstSeen[side] = ms;
      else if (c.status !== 'completed') lastPending[side] = ms;
    }
    return values.web?.status === 'completed' && values.edge?.status === 'completed' && values.web.version === values.edge.version ? values : null;
  });
  const record = { id, route, observed_both_completed_ms: performance.now() - started,
    first_completed_observed_ms: firstSeen, last_noncompleted_observed_ms: lastPending,
    observed_status_history: history, replicas: values };
  (setup ? report.setup_commands : report.commands).push(record); save();
  return record;
}
async function action(route, body = {}, setup = false) {
  const started = performance.now();
  const accepted = await api(serverApi, route, 'POST', body);
  if (route === '/streams/start') currentStream = accepted.stream.id;
  if (route.endsWith('/recordings/start')) activeVideo = accepted.video.id;
  event('command_accepted', { route, id: accepted.command_id, request_ms: performance.now() - started });
  const completion = complete(accepted.command_id, started, route, setup);
  return { accepted, started, completion };
}
async function hashFile(file) {
  const hash = createHash('sha256');
  for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}
function probe(file) {
  return JSON.parse(cp.execFileSync('ffprobe', ['-v', 'error', '-count_frames', '-select_streams', 'v:0',
    '-show_entries', 'stream=codec_name,width,height,nb_frames,nb_read_frames,avg_frame_rate:format=duration,size,bit_rate', '-of', 'json', file], { encoding: 'utf8', timeout: 120000 }));
}
async function observe(duration, label) {
  await page.evaluate(() => { window.__thesisPresentedFrames = null; });
  await page.evaluate(startPresentedCounter);
  await page.waitForFunction(() => Number.isFinite(window.__thesisPresentedFrames), {}, { timeout: 5000 });
  const start = performance.now(), samples = [];
  do {
    const web = await page.evaluate(browserSnapshot);
    const status = await api(control, '/stream/status');
    const media = await api(mediaUrl, '/api/health');
    if (!status.streaming || !status.publisher?.frames_progressing) throw new Error('Camera/publisher stopped progressing');
    samples.push({ web, status, media });
    event('sample', { label, web, status, media });
    await pause(Math.max(0, Math.min(1000, start + duration * 1000 - performance.now())));
  } while (performance.now() - start < duration * 1000);
  samples.push({ web: await page.evaluate(browserSnapshot), status: await api(control, '/stream/status') });
  const metrics = webMetrics(samples[0].web, samples.at(-1).web);
  if (!metrics.rtp_counter_continuity || !(metrics.presented_frames > 0)) throw new Error('Browser counters absent or reset during measured interval');
  return { metrics, start: samples[0].status, end: samples.at(-1).status, additional_viewers: samples.map(s=>s.media?.viewers).filter(Number.isFinite) };
}
async function verifyFile(id) {
  const statuses = [];
  const matched = await until('recording upload and replication', async () => {
    const v = await copies('/videos/' + id, 'video');
    for (const side of ['web', 'edge']) if (v[side]) {
      const previous = statuses.filter(x => x.side === side).at(-1);
      if (previous?.status !== v[side].status || previous?.version !== v[side].version)
        statuses.push({ side, observed_at: new Date().toISOString(), status: v[side].status, version: v[side].version });
    }
    if (Object.values(v).some(x => x?.status === 'failed')) throw new Error('Recording failed');
    return Object.values(v).every(x => x?.status === 'completed') ? v : null;
  }, 180000);
  const fields = ['id', 'stream_id', 'status', 'frame_count', 'dropped_frames', 'size_bytes', 'duration_seconds', 'version'];
  for (const field of fields) if (matched.web[field] !== matched.edge[field]) throw new Error('Replica metadata mismatch: ' + field);
  const original = fs.realpathSync(matched.edge.edge_local_path);
  if (!original.startsWith(path.join(root, 'stream-edge/streaming-service/recordings') + path.sep)) throw new Error('Unexpected original file path');
  const ticket = await api(serverApi, '/videos/' + id + '/playback', 'POST', {});
  const url = new URL(ticket.url);
  if (url.origin !== serverApi) throw new Error('Playback points to wrong server: ' + url.origin);
  const started = performance.now();
  const response = await fetch(url, { signal: AbortSignal.timeout(120000) });
  if (!response.ok) throw new Error('Remote MP4 GET HTTP ' + response.status);
  const destination = path.join(output, 'received', id + '.mp4');
  await pipeline(Readable.fromWeb(response.body), fs.createWriteStream(destination, { flags: 'wx', mode: 0o600 }));
  const downloadMs = performance.now() - started;
  const localHash = await hashFile(original), remoteServedHash = await hashFile(destination);
  if (localHash !== remoteServedHash) throw new Error('Original/server-served MP4 hash mismatch');
  const localProbe = probe(original), remoteProbe = probe(destination);
  const localFrames = Number(localProbe.streams[0].nb_read_frames), remoteFrames = Number(remoteProbe.streams[0].nb_read_frames);
  if (!(localFrames > 0) || localFrames !== remoteFrames || String(localFrames) !== matched.web.frame_count) throw new Error('Decoded frame count mismatch');
  if (fs.statSync(original).size !== Number(matched.web.size_bytes) || fs.statSync(destination).size !== Number(matched.web.size_bytes)) throw new Error('File size mismatch');
  const ranges = await fetch(url, { headers: { Range: 'bytes=0-1023' }, signal: AbortSignal.timeout(10000) });
  const rangeSize = (await ranges.arrayBuffer()).byteLength;
  if (ranges.status !== 206 || rangeSize !== 1024) throw new Error('MP4 byte-range support failed');
  return { id, status_history: statuses, replicas: matched, local_original: original, received_copy: destination,
    sha256_original: localHash, sha256_remote_served: remoteServedHash, hashes_match: true,
    local_probe: localProbe, remote_served_probe: remoteProbe, decoded_frames_match: true,
    range_request_status: ranges.status, verification_download_ms: downloadMs,
    upload_timing: transferTiming(id), independent_server_file_check: 'pending server-session evidence' };
}
function transferTiming(id) {
  const file = path.join(output, 'transfers.jsonl');
  return fs.existsSync(file) ? fs.readFileSync(file, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse).find(x=>x.video_id===id) ?? null : null;
}
async function playbackCheck(id) {
  await page.getByRole('button', { name: /^Recordings/ }).click();
  const video = page.locator(`video[src*="/videos/${id}/file"]`);
  await video.waitFor({ timeout: 15000 });
  const card = page.locator('article').filter({ has: video });
  await card.getByRole('button', { name: 'Play', exact: true }).click();
  await until('recording playback', async()=>video.evaluate(v=>v.currentTime>0.4&&v.videoWidth>0));
  await card.getByRole('button', { name: 'Pause', exact: true }).click();
  const duration = await video.evaluate(v=>v.duration);
  const seek = card.getByLabel('Seek recording');
  await seek.press('Home');
  for(let step=0;step<Math.round(duration/0.2);step++)await seek.press('ArrowRight');
  await until('recording seek', async()=>video.evaluate(v=>Math.abs(v.currentTime-v.duration/2)<0.8));
  await card.getByRole('button', { name: 'Fullscreen', exact: true }).click();
  await until('recording fullscreen', async()=>card.getByRole('button',{name:'Exit fullscreen',exact:true}).getAttribute('aria-pressed').then(x=>x==='true'));
  await page.screenshot({path:path.join(output,'short-recording-fullscreen.png')});
  await card.getByRole('button', { name: 'Exit fullscreen', exact: true }).click();
  await page.screenshot({path:path.join(output,'short-recording-playback.png'),fullPage:true});
  await page.getByRole('button', { name: 'Live', exact: true }).click();
  return { playback: true, pause: true, seek: true, fullscreen: true, duration_s: duration,
    host: os.hostname(), browser_location: 'diagnostic browser on edge laptop, using server-hosted web app' };
}
async function runCycle(index) {
  const cycle = { index, started_at: new Date().toISOString(), status: 'running' };
  report.cycles.push(cycle); save(); log(`${mode} ${index}/${cycles}: starting stream`);
  await until('previous viewer removed', async()=>page.locator('.live-viewer video').count().then(n=>n===0));
  const launch = await action('/streams/start', { device_id: deviceId, title: `LAN ${runId} ${mode} ${index}` });
  cycle.stream_id = launch.accepted.stream.id;
  const framePromise = until('first decoded frame', async()=>{
    const frame = await page.locator('.live-viewer video').evaluateAll(vs=>vs.map(v=>({width:v.videoWidth,height:v.videoHeight,frames:v.getVideoPlaybackQuality().totalVideoFrames})).find(v=>v.frames>0));
    return frame ? { ...frame, observed_startup_ms: performance.now() - launch.started } : null;
  },60000,100);
  const results = await Promise.all([launch.completion,framePromise]);
  cycle.startup = results[1];
  const live = await copies('/streams/' + currentStream, 'stream');
  if (!Object.values(live).every(s=>s.status==='live'&&s.mediasoup_stream_id===currentStream)) throw new Error('LIVE stream replicas disagree');
  cycle.live_replicas = live;
  if(mode==='short'){
    await page.screenshot({path:path.join(output,'short-live.png'),fullPage:true});
    cycle.live_only = await observe(10, 'short-live-only');
  }
  const rec = await action(`/streams/${currentStream}/recordings/start`);
  await rec.completion;cycle.video_id=activeVideo;
  const recordingCopies = await copies('/videos/' + activeVideo, 'video');
  if(!Object.values(recordingCopies).every(v=>v.status==='recording'))throw new Error('Both replicas did not report RECORDING');
  cycle.recording_replicas = recordingCopies;
  cycle.live_recording = await observe(recordSeconds, `${mode}-${index}-recording`);
  const requested = performance.now();
  const stop = await action(`/streams/${currentStream}/recordings/stop`,{video_id:activeVideo});
  await stop.completion;
  const finalized = await api(control,'/record/status');
  cycle.recorder_finalization = finalized.recordingStatus;
  if(!cycle.recorder_finalization.finalized)throw new Error('Recorder not finalized');
  const id=activeVideo;activeVideo=null;
  cycle.file = await verifyFile(id);
  cycle.stop_to_both_completed_and_verification_ms=performance.now()-requested;
  if(mode==='short')cycle.playback=await playbackCheck(id);
  const end=await action(`/streams/${currentStream}/stop`);await end.completion;
  const ended=await copies('/streams/'+currentStream,'stream');
  if(!Object.values(ended).every(s=>s.status==='ended'))throw new Error('Stream did not end on both replicas');
  const idle=await api(control,'/stream/status');
  if(idle.streaming||idle.recording||idle.publisher?.running)throw new Error('Camera did not stop');
  cycle.ended_replicas=ended;currentStream=null;
  cycle.status='passed';cycle.finished_at=new Date().toISOString();save();log(`${mode} ${index}/${cycles}: passed; MP4 ${cycle.video_id}`);
}
async function restartInstrumentedService(name, repo, executable, argv, config) {
  const pidFile=path.join(root,`.runtime/${name}.pid`);
  if(fs.existsSync(pidFile)){
    const pid=Number(fs.readFileSync(pidFile,'utf8'));
    try{
      if(fs.readlinkSync(`/proc/${pid}/cwd`)!==path.join(root,repo))throw new Error('PID ownership mismatch: '+name);
      process.kill(pid,'SIGTERM');
      await until(name+' exits',async()=>{try{process.kill(pid,0);return false;}catch{return true;}},10000,100);
    }catch(e){if(e.code!=='ENOENT'&&e.code!=='ESRCH')throw e;}
  }
  const fd=fs.openSync(path.join(output,name+'.log'),'a',0o600);
  const child=cp.spawn(executable,argv,{cwd:path.join(root,repo),detached:true,env:{...process.env,...config},stdio:['ignore',fd,fd]});
  fs.closeSync(fd);child.unref();if(!child.pid)throw new Error('Could not start '+name);
  fs.writeFileSync(pidFile,child.pid+'\n');
  event('instrumented_service',{name,pid:child.pid});
}
async function main() {
  if(!args.includes('--run') || !['short','cycles'].includes(mode) || !Number.isInteger(cycles) || cycles<1)throw new Error('Use --run --mode short OR --run --mode cycles --cycles 30');
  process.umask(0o077);fs.mkdirSync(output,{recursive:true,mode:0o700});fs.mkdirSync(path.join(output,'received'),{mode:0o700});save();
  report.script_sha256=createHash('sha256').update(fs.readFileSync(__filename)).digest('hex');
  if(env.MEDIA_SERVER_API_URL!==mediaUrl||env.CLOUD_API_URL!==serverApi||streamEnv.EDGE_COMMAND_SYNC_ENABLED!=='0')throw new Error('LAN config mismatch');
  localDb=new(require(path.join(root,'backend-web/node_modules/pg')).Client)({connectionString:env.DATABASE_URL});await localDb.connect();
  const users=(await localDb.query("SELECT id,email,role FROM users WHERE status='ACTIVE' ORDER BY (role='ADMIN') DESC,created_at")).rows;
  const jwt=require(path.join(root,'backend-web/node_modules/jsonwebtoken'));
  const sign=u=>jwt.sign({sub:u.id,email:u.email,role:u.role},env.JWT_SECRET,{expiresIn:'3h'});
  user=users[0];token=sign(user);
  const queue=(await api(serverApi,'/commands')).commands;
  if(queue.some(c=>c.device_id===deviceId&&['pending','claimed','processing'].includes(c.status)))throw new Error('Outstanding commands: coordinate before run');
  const camera=await api(control,'/stream/status');
  if(camera.recording)throw new Error('Existing recording active; refusing interruption');
  const active=(await api(serverApi,'/streams')).streams.find(s=>s.device_id===deviceId&&['live','starting','stopping'].includes(s.status));
  if(active){user=users.find(u=>u.id===active.user_id);if(!user)throw new Error('Cannot use existing stream owner');token=sign(user);const stop=await action(`/streams/${active.id}/stop`,{},true);await stop.completion;}
  else if(camera.streaming)throw new Error('Untracked camera owner; stop through app workflow first');
  const me=await api(serverApi,'/auth/me');report.account_id=user.id;
  await restartInstrumentedService('edge','backend-edge',process.execPath,['dist/src/main.js'],{...env,THESIS_TRANSFER_TIMING_FILE:path.join(output,'transfers.jsonl')});
  await until('backend-edge ready',async()=>{try{return(await fetch(edgeApi+'/auth/me',{signal:AbortSignal.timeout(1000)})).status===401;}catch{return false;}});
  await restartInstrumentedService('qt','frontend-edge',path.join(root,'frontend-edge/build-local/appDiplomaFrontendEdge'),['--benchmark-camera'],{
    DIPLOMA_TEST_API_URL:edgeApi,DIPLOMA_TEST_AUTH_TOKEN:token,DIPLOMA_TEST_OUTPUT:path.join(output,'qt.jsonl')});
  const {chromium}=require(path.join(directory,'node_modules/playwright'));
  browserServer=await chromium.launchServer({executablePath:'/usr/bin/google-chrome',headless:true,args:['--disable-background-timer-throttling','--disable-renderer-backgrounding']});
  browser=await chromium.connect(browserServer.wsEndpoint());
  page=await browser.newPage({viewport:{width:1360,height:900},userAgent:`Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${browser.version()} Safari/537.36`});
  await page.addInitScript(installBrowserProbe,{token,user:me.user});
  page.on('pageerror',e=>event('pageerror',{message:e.message}));
  await page.goto(webUrl,{waitUntil:'domcontentloaded'});
  await page.getByRole('button',{name:'Sign out',exact:true}).waitFor({timeout:30000});
  report.host_evidence={hostname:os.hostname(),interfaces:os.networkInterfaces(),os:fs.readFileSync('/etc/os-release','utf8'),kernel:os.release(),node:process.version,
    listeners:cp.execFileSync('ss',['-ltnp'],{encoding:'utf8'}),connections:cp.execFileSync('ss',['-tnp','dst','192.168.3.162:3000'],{encoding:'utf8'}),repos:{}};
  for(const repo of ['backend-edge','backend-web','frontend-edge','frontend-web','media-server','stream-edge'])report.host_evidence.repos[repo]={commit:cp.execFileSync('git',['rev-parse','HEAD'],{cwd:path.join(root,repo),encoding:'utf8'}).trim(),dirty:!!cp.execFileSync('git',['status','--porcelain'],{cwd:path.join(root,repo),encoding:'utf8'}).trim()};
  report.capture=Object.fromEntries(['STREAMING_CAMERA_WIDTH','STREAMING_CAMERA_HEIGHT','STREAMING_CAMERA_FRAME_RATE','STREAMING_CAMERA_PIXEL_FORMAT','STREAMING_RECORD_FPS','CLOUD_PUBLISH_FRAME_RATE_NUM'].map(k=>[k,streamEnv[k]]));
  resourceChild=cp.spawn('python3',[path.join(directory,'sample_resources.py'),'--root',root,'--out',path.join(output,'resources.jsonl'),'--browser-pid',String(browserServer.process().pid)],{stdio:'ignore'});
  report.status='running';save();
  for(let index=1;index<=cycles;index++)await runCycle(index);
  const entityIds=[...report.commands.map(c=>c.id),...report.cycles.flatMap(c=>[c.stream_id,c.video_id])].filter(Boolean);
  report.local_sync_events={};
  for(const table of ['sync_inbox','sync_outbox'])report.local_sync_events[table]=(await localDb.query(
    `SELECT event_id,entity_type,entity_id,action,version,status,data->>'status' AS entity_status FROM ${table} WHERE entity_id=ANY($1::text[]) ORDER BY entity_id,version`,[entityIds])).rows;
  const qtFile=path.join(output,'qt.jsonl');
  if(fs.existsSync(qtFile)){
    const qt=fs.readFileSync(qtFile,'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
    for(const cycle of report.cycles){
      const starts=Date.parse(cycle.started_at),ends=Date.parse(cycle.finished_at);
      const samples=qt.filter(s=>s.utc_ms>=starts&&s.utc_ms<=ends&&s.live);
      if(samples.length>=2){const first=samples[0],last=samples.at(-1);cycle.qt={interval_s:(last.elapsed_ns-first.elapsed_ns)/1e9,rendered_frames:last.rendered_frames-first.rendered_frames};cycle.qt.rendered_fps=cycle.qt.rendered_frames/cycle.qt.interval_s;}
      else cycle.qt=null;
    }
  }
  const failedSync=(await localDb.query("SELECT 'inbox' AS side,count(*)::int AS n FROM sync_inbox WHERE status='FAILED' UNION ALL SELECT 'outbox',count(*)::int FROM sync_outbox WHERE status='FAILED'")).rows;
  report.local_sync_failed_counts=failedSync;
  report.summary={successful_cycles:report.cycles.filter(c=>c.status==='passed').length,startup_ms:summary(report.cycles.map(c=>c.startup?.observed_startup_ms)),commands:{}};
  for(const name of ['/streams/start','/recordings/start','/recordings/stop','/stop']){
    const values=report.commands.filter(c=>name==='/streams/start'?c.route===name:name==='/stop'?/^\/streams\/[^/]+\/stop$/.test(c.route):c.route.endsWith(name)).map(c=>c.observed_both_completed_ms);
    report.summary.commands[name]=summary(values);
  }
  report.status=failedSync.some(x=>x.n>0)?'completed-with-sync-errors':'passed';
}
async function cleanup() {
  if(activeVideo&&currentStream){try{const stop=await action(`/streams/${currentStream}/recordings/stop`,{video_id:activeVideo},true);await stop.completion;}catch(e){report.recording_cleanup_error=e.message;}}
  if(currentStream){try{const stop=await action(`/streams/${currentStream}/stop`,{},true);await stop.completion;}catch(e){report.stream_cleanup_error=e.message;}}
  if(resourceChild){resourceChild.kill('SIGTERM');await new Promise(resolve=>{resourceChild.once('exit',resolve);setTimeout(resolve,4000);});}
  await browser?.close().catch(()=>{});await browserServer?.close().catch(()=>{});await localDb?.end().catch(()=>{});
}
for(const sig of ['SIGINT','SIGTERM'])process.on(sig,()=>{abortRequested=true;});
main().catch(error=>{report.status='failed';report.error=error.message;const cycle=report.cycles.at(-1);if(cycle?.status==='running'){cycle.status='failed';cycle.error=error.message;}process.exitCode=1;}).finally(async()=>{
  await cleanup();report.finished_at=new Date().toISOString();if(fs.existsSync(output))save();
  log(JSON.stringify({status:report.status,error:report.error,output,summary:report.summary}));
});
