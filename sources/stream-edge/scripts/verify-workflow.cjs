const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const tokens = {};
const results = [];
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

async function api(port, route, method = 'GET', body) {
  const response = await fetch(`http://127.0.0.1:${port}${route}`, {
    method, headers: { 'Content-Type': 'application/json', ...(tokens[port] ? { Authorization: `Bearer ${tokens[port]}` } : {}) },
    body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(25000),
  });
  const data = await response.json();
  if (!response.ok) throw new Error(`${port} ${route}: ${response.status} ${JSON.stringify(data)}`);
  return data;
}
async function until(label, check, timeout = 25000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { if (await check()) return; await pause(250); }
  throw new Error(`Timeout: ${label}`);
}
async function completed(commandId) {
  await until(`command ${commandId} synchronized`, async () => {
    const replies = await Promise.all([3000, 3002].map(port => api(port, '/commands')));
    const commands = replies.map(reply => reply.commands.find(command => command.id === commandId));
    for (const command of commands) if (command?.status === 'failed') throw new Error(command.error_message);
    return commands.every(command => command?.status === 'completed');
  });
}
async function run() {
  const credentials = { email: process.env.THESIS_TEST_EMAIL, password: process.env.THESIS_TEST_PASSWORD };
  if (!credentials.email || !credentials.password) throw new Error('Set THESIS_TEST_EMAIL and THESIS_TEST_PASSWORD');
  for (const port of [3000, 3002]) {
    const auth = await api(port, '/auth/login', 'POST', credentials); tokens[port] = auth.token;
    fs.writeFileSync(path.join(root, `.runtime/auth-${port}.json`), JSON.stringify(auth), { mode: 0o600 });
  }
  results.push('Same cloud account authenticates on edge');
  const previous = (await api(3000, '/streams')).streams.filter(value => value.status === 'live');
  for (const stream of previous) {
    const stop = await api(3000, `/streams/${stream.id}/stop`, 'POST', {});
    await completed(stop.command_id);
  }
  const { stream, command_id } = await api(3000, '/streams/start', 'POST', { device_id: 'edge-dev-001', title: 'Camera workflow verification' });
  await completed(command_id);
  const status = await api(8081, '/stream/status');
  assert.equal(status.publisher.stream_name, stream.id);
  assert.equal(status.publisher.frames_progressing, true);
  assert((await api(3010, '/api/health')).streams.some(value => value.streamId === stream.id));
  results.push('Cloud Start completed on both databases; WHIP stream ID matches');
  const duplicate = await fetch('http://127.0.0.1:3002/streams/start', { method: 'POST', headers: {
    Authorization: `Bearer ${tokens[3002]}`, 'Content-Type': 'application/json',
  }, body: JSON.stringify({ device_id: 'edge-dev-001' }) });
  assert.equal(duplicate.status, 409);
  results.push('Duplicate Start rejected without creating another camera session');
  const recording = await api(3002, `/streams/${stream.id}/recordings/start`, 'POST', {});
  await completed(recording.command_id);
  await until('150 recorded camera frames', async () => (await api(8081, '/record/status')).recordingStatus.written_frames > 150);
  const stopped = await api(3000, `/streams/${stream.id}/recordings/stop`, 'POST', { video_id: recording.video.id });
  await completed(stopped.command_id);
  await until('cloud upload and edge acknowledgment', async () => {
    const copies = await Promise.all([3000, 3002].map(port => api(port, `/videos/${recording.video.id}`)));
    return copies.every(({ video }) => video.status === 'completed');
  }, 45000);
  const { video } = await api(3000, `/videos/${recording.video.id}`);
  const digest = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  assert.equal(digest(video.edge_local_path), digest(video.cloud_path));
  const probe = JSON.parse(execFileSync('ffprobe', ['-v', 'error', '-show_entries', 'stream=codec_name,width,height,nb_frames', '-show_entries', 'format=duration', '-of', 'json', video.cloud_path], { encoding: 'utf8' }));
  assert.equal(probe.streams[0].width, 640);
  assert(Number(probe.format.duration) > 4);
  const playback = await api(3000, `/videos/${video.id}/playback`, 'POST', {});
  const range = await fetch(playback.url, { headers: { Range: 'bytes=0-1023' } });
  assert.equal(range.status, 206);
  assert.equal((await range.arrayBuffer()).byteLength, 1024);
  results.push('Edge recording -> cloud stop -> checksum-verified upload -> synchronized COMPLETED -> authenticated range playback');
  const automatic = await api(3002, `/streams/${stream.id}/recordings/start`, 'POST', {});
  await completed(automatic.command_id);
  await until('60 frames before automatic recording stop', async () =>
    (await api(8081, '/record/status')).recordingStatus.written_frames > 60);
  const stop = await api(3002, `/streams/${stream.id}/stop`, 'POST', {});
  await completed(stop.command_id);
  await until('Stop Stream finalizes and uploads its active recording', async () =>
    (await Promise.all([3000, 3002].map(port => api(port, `/videos/${automatic.video.id}`))))
      .every(reply => reply.video.status === 'completed' && Number(reply.video.frame_count) > 60), 45000);
  results.push('Stop Stream finalizes the active recording before releasing the camera; both replicas receive the uploaded file metadata');
  assert.equal((await api(8081, '/stream/status')).streaming, false);
  await until('media server session cleanup', async () => (await api(3010, '/api/health')).streams.length === 0);
  results.push('Edge Stop completed in cloud and released the camera and media session');
  const next = await api(3002, '/streams/start', 'POST', { device_id: 'edge-dev-001', title: 'Laptop camera' });
  await completed(next.command_id);
  results.push('A second stream starts successfully with a new synchronized stream ID');
  const report = { results, streamId: next.stream.id, videoId: video.id, probe };
  fs.writeFileSync(path.join(root, '.runtime/workflow-result.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
}
run().catch(error => { console.error(error); console.log(results); process.exitCode = 1; });
