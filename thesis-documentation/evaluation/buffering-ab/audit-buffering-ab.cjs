'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const directory = path.resolve(process.argv[2]);
const root = path.resolve(directory, '../../..');
const result = JSON.parse(fs.readFileSync(path.join(directory, 'result.json')));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readRows = file => fs.readFileSync(file, 'utf8').trim().split('\n').map(JSON.parse);
assert.equal(result.status, 'passed');
assert.equal(result.trials.length, 12);
assert.equal(result.commands.length, 48);
for (const command of result.commands) assert.equal(command.status, 'completed');
assert(!result.cleanup_error && !result.database_cleanup_error);
for (const [file, expected] of Object.entries(result.source_hashes)) {
  assert.equal(hash(path.join(root, 'thesis-documentation/evaluation/buffering-ab', file)), expected, file);
}
const conditions = new Set();
const provenance = JSON.parse(fs.readFileSync(path.join(root, '.runtime/buffering-ab/tools-20261026/build-provenance.json')));
assert.equal(hash(provenance.upstream_source), provenance.upstream_sha256);
assert.equal(hash(path.join(root, '.runtime/buffering-ab/tools-20261026/ab-whip-client')), provenance.binary_sha256);
assert.equal(result.source_hashes['ab-probe.c'], provenance.probe_sha256);
assert.equal(result.source_hashes['ab-probe.h'], provenance.header_sha256);
const trials = [];
for (const trial of result.trials) {
  assert.equal(trial.status, 'passed');
  const key = `${trial.load}/${trial.profile}/${trial.replicate}`;
  assert(!conditions.has(key));
  conditions.add(key);
  const rows = readRows(trial.probe_log);
  const inputs = rows.filter(r => r.event === 'input');
  const byId = new Map(inputs.map(r => [r.id, r]));
  assert.equal(byId.size, inputs.length);
  assert.equal(new Set(inputs.map(r => r.pts_ns)).size, inputs.length, `${key}: unique input PTS`);
  for (let i = 0; i < inputs.length; i++) {
    const row = inputs[i];
    assert(Number.isSafeInteger(row.pts_ns) && row.pts_ns >= 0);
    assert.equal(row.id, i + 1);
    assert.equal(row.entered_us, row.mono_us);
  }
  const counts = {};
  for (const event of ['queue_output', 'consumer_output', 'encoded']) {
    const outputs = rows.filter(r => r.event === event);
    let previous = 0;
    for (const row of outputs) {
      const entry = byId.get(row.id);
      assert(entry && row.matched, `${key}: matched ${event}`);
      assert.equal(row.pts_ns, entry.pts_ns);
      assert.equal(row.entered_us, entry.mono_us);
      assert(row.mono_us >= entry.mono_us);
      assert(row.id > previous);
      previous = row.id;
    }
    counts[event] = outputs.length;
  }
  assert(!rows.some(r => r.event === 'safety_abort'));
  assert(trial.probe.interval_s >= 59.8 && trial.probe.interval_s < 65);
  const config = rows.find(r => r.event === 'configuration');
  assert.equal(config.max_buffers, trial.profile === 'latest' ? 2 : 0);
  assert.equal(config.leaky, trial.profile === 'latest' ? 2 : 0);
  assert.equal(config.max_bytes, 0);
  assert.equal(config.max_time_ns, 0);
  assert.equal(config.consumer_sleep_us, trial.load === 'slow' ? 100000 : 0);
  const recording = trial.recording;
  assert.equal(hash(recording.local_path), recording.sha256);
  assert.equal(hash(recording.server_path), recording.sha256);
  assert.equal(fs.statSync(recording.local_path).size, recording.bytes);
  assert.equal(fs.statSync(recording.server_path).size, recording.bytes);
  const diag = recording.recorder_diagnostics;
  assert.equal(diag.finalized, true);
  assert.equal(diag.dropped_frames, 0);
  assert.equal(diag.received_frames, diag.written_frames);
  assert.equal(Number(recording.probe.streams[0].nb_read_frames), diag.written_frames);
  assert.equal(trial.web.rtp_counter_continuity, true);
  trials.push({index:trial.index, condition:key, input_frames:inputs.length, ...counts,
    decoded_recording_frames:diag.written_frames, sha256:recording.sha256});
}
for (const load of ['normal','slow']) for (const profile of ['fifo','latest']) {
  for (let rep=1; rep<=3; rep++) assert(conditions.has(`${load}/${profile}/${rep}`));
}
const audit = {status:'passed', run_id:result.run_id, audited_at:new Date().toISOString(),
  source_hashes_match:true, native_pts_and_entry_matches:true,
  actual_recording_hashes_recomputed:true, trials};
fs.writeFileSync(path.join(directory, 'independent-audit.json'), JSON.stringify(audit,null,2)+'\n');
console.log(JSON.stringify({status:audit.status, trials:trials.length, commands:result.commands.length,
  recording_frames:trials.reduce((s,t)=>s+t.decoded_recording_frames,0)},null,2));
