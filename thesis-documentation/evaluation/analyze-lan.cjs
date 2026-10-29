const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = process.cwd();
const { summary } = require(path.join(root, 'thesis-documentation/evaluation/metrics.cjs'));
function group(route) {
  if (route === '/streams/start') return 'stream_start';
  if (/^\/streams\/[^/]+\/recordings\/start$/.test(route)) return 'recording_start';
  if (/^\/streams\/[^/]+\/recordings\/stop$/.test(route)) return 'recording_stop';
  if (/^\/streams\/[^/]+\/stop$/.test(route)) return 'stream_stop';
  return null;
}
function aggregate(run) {
  const successful = run.cycles.filter(c => c.status === 'passed');
  const intervals = successful.map(c => c.live_recording.metrics);
  const seconds = intervals.reduce((n, x) => n + x.interval_s, 0);
  const total = key => intervals.every(x => Number.isFinite(x[key])) ? intervals.reduce((n, x) => n + x[key], 0) : null;
  const commandGroups = {};
  for (const name of ['stream_start', 'recording_start', 'recording_stop', 'stream_stop'])
    commandGroups[name] = summary(run.commands.filter(c => group(c.route) === name).map(c => c.observed_both_completed_ms));
  return { requested_cycles: run.requested_cycles, passed_cycles: successful.length, failed_cycles: run.cycles.filter(c => c.status === 'failed').length,
    measured_commands: run.commands.length, command_ms: commandGroups,
    startup_ms: summary(successful.map(c => c.startup.observed_startup_ms)),
    browser_recording_observations: { total_interval_s: seconds,
      presented_fps_weighted: seconds && total('presented_frames') !== null ? total('presented_frames') / seconds : null,
      decoded_fps_weighted: seconds && total('decoded_frames') !== null ? total('decoded_frames') / seconds : null,
      playback_dropped_frames: total('playback_dropped_frames'), playback_total_frames: total('playback_total_frames'),
      playback_drop_percent: total('playback_total_frames') && total('playback_dropped_frames') !== null ? total('playback_dropped_frames') / total('playback_total_frames') * 100 : null,
      packets_lost_delta: total('packets_lost_delta'),
      received_rtp_mbps_weighted: seconds && intervals.every(x=>Number.isFinite(x.received_rtp_mbps)) ? intervals.reduce((n, x) => n + x.received_rtp_mbps * x.interval_s, 0) / seconds : null },
    qt_per_cycle_fps: summary(successful.map(c => c.qt?.rendered_fps)),
    finalization_ms: summary(successful.map(c => c.recorder_finalization?.finalization_duration_ms)),
    upload_request_ms: summary(successful.map(c => c.file?.upload_timing?.request_duration_ms)),
    recorder_queue_drops: successful.reduce((n, c) => n + c.recorder_finalization.dropped_frames, 0),
    hash_matches: successful.filter(c => c.file.hashes_match).length,
    decoded_frame_matches: successful.filter(c => c.file.decoded_frames_match).length,
    total_recorded_frames: successful.reduce((n, c) => n + Number(c.file.local_probe.streams[0].nb_read_frames), 0) };
}
function resources(directory) {
  const file = path.join(directory, 'resources.jsonl');
  if (!fs.existsSync(file)) return null;
  const values = fs.readFileSync(file, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
  const names = new Set(values.flatMap(s => Object.keys(s.components)));
  return { scope: 'Whole run on edge laptop, including startup, recording, upload, verification downloads and FFprobe; not a steady-state baseline comparison.',
    components: Object.fromEntries([...names].map(name => [name, {
      cpu_percent_one_core: summary(values.filter(s => s.valid_cpu_interval).map(s => s.components[name]?.cpu_percent)),
      rss_mib: summary(values.map(s => s.components[name]?.rss_bytes / 1024 ** 2)) }])) };
}
function main() {
  if (process.argv.includes('--self-test')) {
    assert.equal(group('/streams/x/recordings/stop'), 'recording_stop');
    assert.equal(group('/streams/x/stop'), 'stream_stop');
    assert.equal(group('/streams/start'), 'stream_start');
    assert.equal(group('/streams/x/recordings/start'), 'recording_start');
    assert.equal(group('/stop'), null);
    const sample = aggregate({ requested_cycles: 0, cycles: [], commands: [] });
    assert.equal(sample.startup_ms.n, 0);
    assert.equal(sample.browser_recording_observations.presented_fps_weighted, null);
    console.log('PASS: disjoint command groups, empty measurements and null metrics'); return;
  }
  const directories = process.argv.slice(2).map(p => path.resolve(p));
  if (!directories.length) throw new Error('Pass one or more completed LAN evidence directories');
  for (const directory of directories) {
    const run = JSON.parse(fs.readFileSync(path.join(directory, 'result.json')));
    if (!['passed', 'completed-with-sync-errors'].includes(run.status)) throw new Error('Run is not complete: ' + directory);
    const result = { source: directory, source_status: run.status, ...aggregate(run), local_resources: resources(directory) };
    fs.writeFileSync(path.join(directory, 'analysis.json'), JSON.stringify(result, null, 2) + '\n', { mode: 0o600 });
    const rows = [['command', 'n', 'mean_ms', 'p50_ms', 'p95_ms', 'min_ms', 'max_ms']];
    for (const [name, stats] of Object.entries(result.command_ms)) rows.push([name, stats.n, stats.mean, stats.p50, stats.p95, stats.min, stats.max]);
    fs.writeFileSync(path.join(directory, 'command-timings.csv'), rows.map(r => r.join(',')).join('\n') + '\n', { mode: 0o600 });
    console.log(JSON.stringify({ directory, ...result, local_resources: undefined }, null, 2));
  }
}
main();
