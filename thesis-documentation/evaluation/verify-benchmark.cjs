const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const { createHash, randomBytes } = require('node:crypto');
const { webMetrics } = require('./metrics.cjs');
const { installBrowserProbe, startPresentedCounter, browserSnapshot, lockMeasurementInput } = require('./browser-probe.cjs');

const args = process.argv.slice(2);
const option = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const root = path.resolve(option('--root', path.join(__dirname, '../..')));
const runId = new Date().toISOString().replace(/[:.]/g, '-');
const output = path.resolve(option('--output', path.join(root, '.runtime/evaluation', runId)));
const warmup = Number(option('--warmup-seconds', '15'));
const sampleSeconds = Number(option('--sample-seconds', '60'));
const longSeconds = Number(option('--long-seconds', '600'));
const replicates = Number(option('--replicates', '3'));
const webUrl = option('--web-url', 'http://localhost:3001');
const webApi = option('--web-api', 'http://127.0.0.1:3000');
const edgeApi = option('--edge-api', 'http://127.0.0.1:3002');
const control = option('--control-url', 'http://127.0.0.1:8081');
const deviceId = option('--device-id', 'edge-dev-001');
const tokens = {};
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const children = [];
const commands = [];
const recordingReports = [];
const conditions = [];
let browser, server, streamId, activeRecording, page;
let credentials;
let cleaningUp = false;
const report = { schema_version: 1, run_id: runId, status: 'running', conditions, recordings: recordingReports, commands,
  protocol: { warmup_s: warmup, sample_s: sampleSeconds, long_recording_s: longSeconds, replicates,
    web_url: webUrl, web_api: webApi, edge_api: edgeApi, control_url: control, headed: args.includes('--headed'),
    baseline: 'same stream, one Qt view and one browser viewer; recording disabled versus enabled',
    optical_latency: 'not measured by this runner', command_cycles: 'not the 30-cycle test required by comment 18',
    cpu_convention: '100 percent = one logical core; RSS sums may double-count shared pages' } };

function save() { fs.writeFileSync(path.join(output, 'benchmark-result.json'), JSON.stringify(report, null, 2)); }
function log(message) { console.log(`[${new Date().toISOString()}] ${message}`); }
function phase(name) { report.phase = name; save(); log(name); }
function browserEvent(kind, detail) {
  fs.appendFileSync(path.join(output, 'browser-events.jsonl'), JSON.stringify({ utc: new Date().toISOString(), kind, ...detail }) + '\n', { mode: 0o600 });
}

async function api(base, route, method = 'GET', body) {
  const response = await fetch(base + route, { method, headers: { 'Content-Type': 'application/json',
    ...(tokens[base] ? { Authorization: `Bearer ${tokens[base]}` } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(90000) });
  const payload = await response.json();
  if (!response.ok) throw new Error(`${method} ${route}: HTTP ${response.status}: ${JSON.stringify(payload)}`);
  return payload;
}

async function until(label, check, timeout = 90000) {
  const deadline = performance.now() + timeout;
  while (performance.now() < deadline) {
    const result = await check();
    if (result) return result;
    await pause(200);
  }
  throw new Error(`Timeout: ${label}`);
}

async function completed(id) {
  return until(`command ${id}`, async () => {
    const copies = await Promise.all([webApi, edgeApi].map(async base =>
      (await api(base, '/commands')).commands.find(value => value.id === id)));
    const failed = copies.find(value => value?.status === 'failed');
    if (failed) throw new Error(failed.error_message || 'Command failed');
    return copies.every(value => value?.status === 'completed') ? copies : null;
  });
}

async function action(route, body = {}) {
  const start = performance.now();
  const accepted = await api(webApi, route, 'POST', body);
  if (route === '/streams/start') streamId = accepted.stream.id;
  if (route.endsWith('/recordings/start')) activeRecording = accepted.video.id;
  const copies = await completed(accepted.command_id);
  commands.push({ route, id: accepted.command_id, observed_completion_ms: performance.now() - start,
    poll_interval_ms: 200, replicas: copies.map(value => ({ status: value.status, version: value.version })) });
  save();
  return accepted;
}

async function authenticate() {
  if (process.env.THESIS_TEST_EMAIL && process.env.THESIS_TEST_PASSWORD) {
    credentials = { email: process.env.THESIS_TEST_EMAIL, password: process.env.THESIS_TEST_PASSWORD };
    tokens[webApi] = (await api(webApi, '/auth/login', 'POST', credentials)).token;
  } else if (args.includes('--create-test-user')) {
    credentials = { email: `measurement-${runId}@local.test`, password: randomBytes(18).toString('hex') };
    tokens[webApi] = (await api(webApi, '/auth/register', 'POST', {
      ...credentials, fullName: 'Thesis measurement account', role: 'OPERATOR' })).token;
  } else throw new Error('Set THESIS_TEST_EMAIL/PASSWORD or explicitly pass --create-test-user');
  await until('account replication', async () => {
    try { tokens[edgeApi] = (await api(edgeApi, '/auth/login', 'POST', credentials)).token; return true; }
    catch { return false; }
  });
}

function startChild(command, commandArgs, filename, env = {}, cwd = root) {
  const fd = fs.openSync(path.join(output, filename), 'w', 0o600);
  const child = spawn(command, commandArgs, { cwd, env: { ...process.env, ...env }, stdio: ['ignore', fd, fd] });
  fs.closeSync(fd);
  children.push(child);
  child.on('error', error => { report.child_error = error.message; });
  return child;
}

async function snapshot() {
  return page.evaluate(browserSnapshot);
}

async function observe(name, recording, duration) {
  report.phase = name; save();
  log(`${name}: ${warmup}s warmup, then ${duration}s measurement`);
  await pause(warmup * 1000);
  if (['live-only-1', 'live-record-1', 'long-recording'].includes(name))
    await page.screenshot({ path: path.join(output, `web-${name}.png`), fullPage: true });
  const samples = [], started = performance.now(), deadline = started + duration * 1000;
  let lastProgress = started;
  do {
    const [web, status] = await Promise.all([snapshot(), api(control, '/stream/status')]);
    if (!status.streaming || status.status === 'error') throw new Error('Streaming failed during measurement');
    if (recording && !status.recordingStatus.active) throw new Error('Recording stopped before the measurement finished');
    samples.push({ web, status });
    fs.appendFileSync(path.join(output, 'samples.jsonl'), JSON.stringify({ condition: name, web, status }) + '\n');
    if (performance.now() - lastProgress >= 15000) {
      log(`${name}: ${Math.round((performance.now() - started) / 1000)}/${duration}s sampled; ${web.presented_frames} browser frames`);
      lastProgress = performance.now();
    }
    await pause(Math.max(0, Math.min(1000, deadline - performance.now())));
  } while (performance.now() < deadline);
  samples.push({ web: await snapshot(), status: await api(control, '/stream/status') });
  fs.appendFileSync(path.join(output, 'samples.jsonl'), JSON.stringify({ condition: name, ...samples.at(-1) }) + '\n');
  const metrics = webMetrics(samples[0].web, samples.at(-1).web);
  if (!(metrics.presented_frames > 0)) throw new Error('No presented video frames during the interval');
  if (!metrics.rtp_counter_continuity) throw new Error('WebRTC stream changed during interval; measurement invalid');
  const condition = { name, recording, ...metrics, utc_start_ms: samples[0].web.utc_ms,
    utc_end_ms: samples.at(-1).web.utc_ms, sample_count: samples.length,
    pixel_range_min: Math.min(...samples.map(value => value.web.pixel_range)), pixel_range_threshold: 5,
    nonuniform_image_check_passed: samples.every(value => value.web.pixel_range >= 5),
    recording_start: samples[0].status.recordingStatus, recording_end: samples.at(-1).status.recordingStatus };
  conditions.push(condition); save();
  log(`${name}: ${metrics.presented_fps.toFixed(2)} presented FPS, ${metrics.received_rtp_mbps?.toFixed(3)} Mbit/s received RTP`);
  return condition;
}

async function startRecording() {
  const accepted = await action(`/streams/${streamId}/recordings/start`);
  activeRecording = accepted.video.id;
  await until('recorder active', async () => (await api(control, '/record/status')).recordingStatus.active);
  const status = await api(control, '/record/status');
  return { id: activeRecording, utc_start_ms: Date.now(), started_monotonic_ms: performance.now(), status };
}

async function stopRecording(start) {
  const stopRequested = performance.now();
  await action(`/streams/${streamId}/recordings/stop`, { video_id: start.id });
  activeRecording = null;
  const finalized = (await api(control, '/record/status')).recordingStatus;
  if (!finalized.finalized) throw new Error('Recorder did not finalize');
  const finalizedObserved = performance.now();
  const copies = await until('uploaded recording on both replicas', async () => {
    const videos = await Promise.all([webApi, edgeApi].map(base => api(base, `/videos/${start.id}`)));
    return videos.every(value => value.video.status === 'completed') ? videos.map(value => value.video) : null;
  }, 360000);
  const uploadObserved = performance.now();
  const video = copies[0];
  const hashFile = async file => {
    const digest = createHash('sha256');
    for await (const chunk of fs.createReadStream(file)) digest.update(chunk);
    return digest.digest('hex');
  };
  const [edgeHash, serverHash] = await Promise.all([hashFile(video.edge_local_path), hashFile(video.cloud_path)]);
  if (edgeHash !== serverHash) throw new Error('Edge/server SHA-256 mismatch');
  if (copies[0].frame_count !== copies[1].frame_count || copies[0].size_bytes !== copies[1].size_bytes)
    throw new Error('Recording metadata mismatch between replicas');
  const probe = JSON.parse(execFileSync('ffprobe', ['-v', 'error', '-show_entries',
    'stream=codec_name,width,height,nb_frames,avg_frame_rate:format=duration,size,bit_rate', '-of', 'json', video.cloud_path], { encoding: 'utf8' }));
  const transfers = process.env.THESIS_TRANSFER_TIMING_FILE && fs.existsSync(process.env.THESIS_TRANSFER_TIMING_FILE)
    ? fs.readFileSync(process.env.THESIS_TRANSFER_TIMING_FILE, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse).filter(value => value.video_id === start.id) : [];
  const result = { id: start.id, observed_active_duration_s: (stopRequested - start.started_monotonic_ms) / 1000,
    stop_to_finalized_observed_ms: finalizedObserved - stopRequested,
    finalized_to_both_replicas_completed_observed_ms: uploadObserved - finalizedObserved,
    finalization_duration_ms: finalized.finalization_duration_ms ?? null,
    writer_stop_duration_ms: finalized.writer_stop_duration_ms ?? null,
    transfer_timing: transfers.at(-1) ?? null, size_bytes: fs.statSync(video.cloud_path).size,
    sha256_edge: edgeHash, sha256_server: serverHash, sha256_match: edgeHash === serverHash,
    recorder_diagnostics: finalized, probe, metadata: { duration_seconds: video.duration_seconds,
      frame_count: video.frame_count, dropped_frames: video.dropped_frames, version: video.version },
    notes: ['Observed stop timing includes command routing and polling; internal finalization is reported separately.',
      'Upload request timing includes response and server checksum/storage work; polling convergence is not pure transfer time.',
      'This runner hashes two local paths and targets the one-laptop configuration, not the distributed test.'] };
  recordingReports.push(result); save();
  return result;
}

async function run() {
  if (!args.includes('--camera') && !args.includes('--preflight')) throw new Error('Use --camera or --preflight.');
  if (!(sampleSeconds > 0 && longSeconds >= 600 && Number.isInteger(replicates) && replicates > 0 && warmup >= 0))
    throw new Error('Require positive sample time/replicates, nonnegative warmup and long recording >=600 seconds');
  fs.mkdirSync(output, { recursive: true }); save();
  execFileSync('python3', [path.join(__dirname, 'collect_hardware.py'), '--root', root, '--out', path.join(output, 'hardware.json')]);
  const hardware = JSON.parse(fs.readFileSync(path.join(output, 'hardware.json'), 'utf8'));
  const cameraConfig = hardware.capture_configuration;
  report.capture = { requested: cameraConfig,
    model: hardware.camera_settings?.stdout?.match(/Card type\s*:\s*(.+)/)?.[1]?.trim() ?? null };
  phase('Authenticating test account and checking replication');
  await authenticate();
  const existing = (await api(webApi, '/streams')).streams.filter(value => ['live', 'starting', 'stopping'].includes(value.status));
  if (existing.some(value => value.device_id === deviceId)) throw new Error('Device already has an active stream. Stop it in the app first; no queues are cleared by this test.');
  const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
  server = await chromium.launchServer({ executablePath: process.env.CHROME_PATH || '/usr/bin/google-chrome',
    headless: !args.includes('--headed'), args: ['--disable-background-timer-throttling', '--disable-renderer-backgrounding'] });
  browser = await chromium.connect(server.wsEndpoint());
  page = await browser.newPage({ viewport: { width: 1360, height: 900 } });
  const errors = []; report.browser_errors = errors;
  page.on('dialog', async dialog => {
    browserEvent('dialog-dismissed', { type: dialog.type() });
    await dialog.dismiss();
  });
  page.on('pageerror', error => { errors.push(error.message); browserEvent('pageerror', { message: error.message }); });
  page.on('console', message => {
    if (['error', 'warning'].includes(message.type())) browserEvent('console', { level: message.type(), message: message.text() });
  });
  page.on('requestfailed', request => {
    const url = new URL(request.url()); url.search = ''; url.hash = '';
    browserEvent('requestfailed', { url: url.href, error: request.failure()?.errorText });
  });
  page.on('response', response => {
    if (response.status() >= 400) {
      const url = new URL(response.url()); url.search = ''; url.hash = '';
      browserEvent('http-error', { url: url.href, status: response.status() });
    }
  });
  await page.addInitScript(installBrowserProbe, {});
  phase('Loading web login page');
  await page.goto(webUrl, { waitUntil: 'domcontentloaded' });
  await page.getByLabel('Email', { exact: true }).waitFor({ timeout: 60000 });
  await page.getByLabel('Email', { exact: true }).fill(credentials.email);
  await page.getByLabel('Password', { exact: true }).fill(credentials.password);
  phase('Signing in through the actual web interface');
  await page.getByRole('button', { name: 'Enter Console', exact: true }).click();
  await page.getByRole('button', { name: 'Sign out', exact: true }).waitFor({ timeout: 60000 });
  if (errors.length) throw new Error('Browser startup JavaScript errors; inspect browser-events.jsonl');
  phase('Web dashboard authenticated');
  if (args.includes('--preflight')) {
    await page.screenshot({ path: path.join(output, 'preflight-dashboard.png'), fullPage: true });
    report.status = 'preflight-passed'; report.finished_utc = new Date().toISOString(); save();
    log('PASS: browser login and dashboard verified; no camera stream requested');
    return;
  }
  await page.evaluate(lockMeasurementInput);
  report.protocol.manual_browser_input = 'blocked after login to preserve the fixed live-view condition';
  startChild('python3', [path.join(__dirname, 'sample_resources.py'), '--root', root, '--out', path.join(output, 'resources.jsonl'),
    '--browser-pid', String(server.process().pid)], 'resource-sampler.log');
  startChild(path.join(root, 'frontend-edge/build-local/appDiplomaFrontendEdge'), ['--benchmark-camera'], 'qt.log', {
    DIPLOMA_TEST_API_URL: edgeApi, DIPLOMA_TEST_AUTH_TOKEN: tokens[edgeApi],
    DIPLOMA_TEST_OUTPUT: path.join(output, 'qt.jsonl'), XDG_CONFIG_HOME: path.join(output, 'private-qt-settings') },
    path.join(root, 'frontend-edge'));
  phase('Starting the real camera and waiting for Qt/browser video');
  const start = await action('/streams/start', { device_id: deviceId, title: 'Thesis quantitative evaluation' });
  streamId = start.stream.id; report.stream_id = streamId;
  await page.waitForFunction(() => {
    const video = document.querySelector('.live-viewer video');
    return video?.videoWidth > 0 && video.getVideoPlaybackQuality().totalVideoFrames > 10;
  }, null, { timeout: 90000 });
  await page.evaluate(startPresentedCounter);
  await until('Qt displays camera frames', async () => {
    const qtPath = path.join(output, 'qt.jsonl');
    if (!fs.existsSync(qtPath)) return false;
    const text = fs.readFileSync(qtPath, 'utf8').trim();
    if (!text) return false;
    const sample = JSON.parse(text.split('\n').at(-1));
    return sample.live && sample.rendered_frames > 10;
  });
  const firstFrame = await snapshot();
  if (Number(cameraConfig.STREAMING_CAMERA_WIDTH) !== firstFrame.width ||
      Number(cameraConfig.STREAMING_CAMERA_HEIGHT) !== firstFrame.height)
    throw new Error('Observed video dimensions do not match configured camera mode');
  const activeCameraSettings = execFileSync('v4l2-ctl', ['--device', cameraConfig.STREAMING_CAMERA_DEVICE, '--all'], { encoding: 'utf8' });
  fs.writeFileSync(path.join(output, 'camera-settings-active.txt'), activeCameraSettings);
  report.capture.observed_width = firstFrame.width;
  report.capture.observed_height = firstFrame.height;
  report.capture.dynamic_frame_rate_control_exposed = /\bexposure_dynamic_framerate\b/.test(activeCameraSettings);
  save();
  for (let repeat = 1; repeat <= replicates; repeat++) {
    const order = repeat % 2 ? [false, true] : [true, false];
    for (const recording of order) {
      const started = recording ? await startRecording() : null;
      await observe(`${recording ? 'live-record' : 'live-only'}-${repeat}`, recording, sampleSeconds);
      if (started) await stopRecording(started);
    }
  }
  const long = await startRecording();
  await observe('long-recording', true, longSeconds);
  const longReport = await stopRecording(long);
  if (Number(longReport.probe.format.duration) < 600) throw new Error('MP4 duration is below the required 10-minute minimum');
  if (errors.length) throw new Error('Browser JavaScript errors occurred; inspect browser_errors in the report');
  report.status = 'passed'; report.finished_utc = new Date().toISOString(); save();
  log(`PASS. Raw data: ${output}`);
}

async function cleanup() {
  if (cleaningUp) return;
  cleaningUp = true;
  if (activeRecording && streamId) {
    await action(`/streams/${streamId}/recordings/stop`, { video_id: activeRecording }).catch(() => {});
  }
  if (streamId) await action(`/streams/${streamId}/stop`).catch(error => { report.cleanup_error = error.message; });
  for (const child of children) {
    child.kill('SIGTERM');
    await Promise.race([new Promise(resolve => child.once('exit', resolve)), pause(3000)]);
    if (child.exitCode === null) child.kill('SIGKILL');
  }
  await browser?.close(); await server?.close();
}

for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => {
  report.status = 'interrupted'; report.error = signal;
  cleanup().finally(() => { if (fs.existsSync(output)) save(); process.exit(130); });
});

if (args.includes('--help') || !args.length) {
  console.log('Usage: node verify-benchmark.cjs --camera --headed --create-test-user [--root PATH] [--output PATH]');
  console.log('Defaults: 3 alternating live-only/live+record replicates, 15s warmup, 60s samples, then >=600s recording.');
  console.log('Start the stack first. Set THESIS_TRANSFER_TIMING_FILE in backend-edge and this runner for exact request timing.');
  console.log('This captures the real camera, creates local recordings and requires roughly 18 minutes plus startup/transfer time.');
} else {
  run().catch(async error => { report.status = 'failed'; report.error = error.message; process.exitCode = 1;
    if (page && !page.isClosed()) {
      report.failure_page = await page.locator('body').innerText({ timeout: 3000 }).catch(() => 'Page text unavailable');
      await page.screenshot({ path: path.join(output, 'failure.png'), fullPage: true, timeout: 5000 }).catch(() => {});
    }
    if (fs.existsSync(output)) save(); console.error(error.message); }).finally(async () => { await cleanup(); if (fs.existsSync(output)) save(); });
}
