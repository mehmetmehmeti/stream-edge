const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { randomUUID } = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const tokens = {};
const results = [];
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function api(port, route, method = 'GET', body) {
  const response = await fetch(`http://127.0.0.1:${port}${route}`, { method,
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${tokens[port]}` },
    body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(20000) });
  const payload = await response.json(); if (!response.ok) throw new Error(JSON.stringify(payload)); return payload;
}
async function until(check, timeout = 30000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) { try { if (await check()) return; } catch {} await pause(250); }
  throw new Error('Recovery check timed out');
}
function stop(name) { process.kill(Number(fs.readFileSync(path.join(root, `.runtime/${name}.pid`), 'utf8')), 'SIGTERM'); }
function start(name) {
  const fd = fs.openSync(path.join(root, `.runtime/${name}.log`), 'a');
  const repo = path.join(root, `diploma-2026-backend-${name}`);
  const config = require(path.join(repo, 'node_modules/dotenv')).parse(fs.readFileSync(path.join(repo, '.env')));
  const child = spawn(process.execPath, ['dist/src/main.js'], { cwd: repo, env: { ...process.env, ...config },
    detached: true, stdio: ['ignore', fd, fd] }); child.unref(); fs.closeSync(fd);
  fs.writeFileSync(path.join(root, `.runtime/${name}.pid`), String(child.pid));
}
async function completed(id) {
  await until(async () => (await Promise.all([3000,3002].map(port => api(port, '/commands'))))
    .every(reply => reply.commands.find(command => command.id === id)?.status === 'completed'));
}
(async () => {
  if (!process.env.THESIS_TEST_EMAIL || !process.env.THESIS_TEST_PASSWORD)
    throw new Error('Set THESIS_TEST_EMAIL and THESIS_TEST_PASSWORD');
  for (const port of [3000, 3002]) tokens[port] = (await api(port, '/auth/login', 'POST', {
    email: process.env.THESIS_TEST_EMAIL, password: process.env.THESIS_TEST_PASSWORD })).token;
  const live = (await api(3000, '/streams')).streams.find(stream => stream.status === 'live');
  assert(live, 'Start a camera stream before running recovery verification');
  stop('edge'); await pause(500);
  const stopped = await api(3000, `/streams/${live.id}/stop`, 'POST', {});
  await pause(1500);
  assert.equal((await api(3000, `/commands/${stopped.command_id}`)).command.status, 'pending');
  start('edge'); await completed(stopped.command_id);
  assert.equal((await api(8081, '/stream/status')).streaming, false);
  results.push('Cloud Stop stayed pending while edge backend was offline and completed after reconnect');

  const edgeRepo = path.join(root, 'diploma-2026-backend-edge');
  const env = require(path.join(edgeRepo, 'node_modules/dotenv')).parse(fs.readFileSync(path.join(edgeRepo, '.env')));
  const previousDatabaseUrl = process.env.DATABASE_URL;
  process.env.DATABASE_URL = env.DATABASE_URL;
  const { PrismaService } = require(path.join(edgeRepo, 'dist/src/prisma/prisma.service.js'));
  const { SyncService } = require(path.join(edgeRepo, 'dist/src/sync/sync.service.js'));
  const prisma = new PrismaService();
  if (previousDatabaseUrl === undefined) delete process.env.DATABASE_URL;
  else process.env.DATABASE_URL = previousDatabaseUrl;
  try {
    const before = await prisma.command.findUniqueOrThrow({ where: { id: stopped.command_id } });
    const command = (await api(3002, `/commands/${stopped.command_id}`)).command;
    const event = { type: 'event', event_id: randomUUID(), source: 'cloud', entity_type: 'command',
      entity_id: stopped.command_id, action: 'create', version: 1, occurred_at: command.created_at,
      data: { ...command, status: 'pending', version: 1, updated_at: command.created_at } };
    const sync = new SyncService(prisma);
    assert.equal((await sync.processEvent(event)).message, 'Stale entity version ignored');
    assert.equal((await sync.processEvent(event)).message, 'Duplicate event ignored');
    const after = await prisma.command.findUniqueOrThrow({ where: { id: stopped.command_id } });
    assert.equal(after.status, 'COMPLETED'); assert.equal(+after.processedAt, +before.processedAt);
    results.push('A delayed PENDING event and its duplicate cannot replay a completed command');
  } finally { await prisma.$disconnect(); }

  const next = await api(3002, '/streams/start', 'POST', { device_id: 'edge-dev-001', title: 'Recovery camera' });
  await completed(next.command_id);
  const recording = await api(3002, `/streams/${next.stream.id}/recordings/start`, 'POST', {});
  await completed(recording.command_id);
  await until(async () => (await api(8081, '/record/status')).recordingStatus.written_frames > 90);
  stop('cloud'); await pause(500);
  const recordStop = await api(3002, `/streams/${next.stream.id}/recordings/stop`, 'POST', { video_id: recording.video.id });
  await until(async () => (await api(3002, `/commands/${recordStop.command_id}`)).command.status === 'completed');
  await pause(5500);
  assert.equal((await api(3002, `/videos/${recording.video.id}`)).video.status, 'uploading');
  start('cloud');
  await until(async () => (await Promise.all([3000,3002].map(port => api(port, `/videos/${recording.video.id}`))))
    .every(reply => reply.video.status === 'completed'), 45000);
  await completed(recordStop.command_id);
  const copies = await Promise.all([3000,3002].map(port => api(port, `/videos/${recording.video.id}`)));
  for (const field of ['status','frame_count','size_bytes','cloud_path','playback_url'])
    assert.equal(copies[0].video[field], copies[1].video[field], field);
  assert(Number(copies[0].video.frame_count) > 90);
  results.push('Recording survived cloud outage; upload retried; file metadata and frame counts converged on both replicas');
  const finalStop = await api(3000, `/streams/${next.stream.id}/stop`, 'POST', {});
  await completed(finalStop.command_id);
  fs.writeFileSync(path.join(root, '.runtime/recovery-result.json'), JSON.stringify({ results }, null, 2));
  console.log(JSON.stringify({ results }, null, 2));
})().catch(error => { console.error(error); console.log(results); process.exitCode = 1; });
