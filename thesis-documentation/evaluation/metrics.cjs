const assert = require('node:assert/strict');

function delta(first, last, key) {
  if (!Number.isFinite(first?.[key]) || !Number.isFinite(last?.[key])) return null;
  const result = last[key] - first[key];
  return result >= 0 ? result : null;
}

function webMetrics(first, last) {
  const seconds = (last.elapsed_ms - first.elapsed_ms) / 1000;
  if (!(seconds > 0)) throw new Error('Measurement interval must be positive');
  const presented = delta(first, last, 'presented_frames');
  const dropped = delta(first, last, 'playback_dropped_frames');
  const total = delta(first, last, 'playback_total_frames');
  const sameRtp = first.rtp?.id && first.rtp.id === last.rtp?.id;
  const decoded = sameRtp ? delta(first.rtp, last.rtp, 'framesDecoded') : null;
  const received = sameRtp ? delta(first.rtp, last.rtp, 'framesReceived') : null;
  const bytes = sameRtp ? delta(first.rtp, last.rtp, 'bytesReceived') : null;
  return { interval_s: seconds, presented_frames: presented, presented_fps: presented === null ? null : presented / seconds,
    decoded_frames: decoded, decoded_fps: decoded === null ? null : decoded / seconds,
    received_frames: received, playback_total_frames: total, playback_dropped_frames: dropped,
    playback_drop_percent: total > 0 && dropped !== null ? dropped / total * 100 : null,
    received_rtp_mbps: bytes === null ? null : bytes * 8 / seconds / 1e6,
    packets_lost_delta: sameRtp && Number.isFinite(last.rtp.packetsLost) && Number.isFinite(first.rtp.packetsLost)
      ? last.rtp.packetsLost - first.rtp.packetsLost : null,
    rtp_counter_continuity: Boolean(sameRtp),
    notes: ['Presented frames use requestVideoFrameCallback metadata.presentedFrames.',
      'Playback drops and RTP packet loss are separate metrics.',
      'RTP bitrate uses bytesReceived, not the encoder target bitrate.',
      'Missing counters and counter resets are null, not zero.'] };
}

function summary(values) {
  const sorted = values.filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length) return { n: 0, mean: null, p50: null, p95: null, min: null, max: null };
  const median = sorted.length % 2 ? sorted[(sorted.length - 1) / 2] : (sorted[sorted.length / 2 - 1] + sorted[sorted.length / 2]) / 2;
  return { n: sorted.length, mean: sorted.reduce((a, b) => a + b, 0) / sorted.length,
    p50: median, p95: sorted[Math.ceil(0.95 * sorted.length) - 1], min: sorted[0], max: sorted.at(-1) };
}

module.exports = { delta, webMetrics, summary };

if (require.main === module) {
  assert.equal(delta({ a: 4 }, { a: 10 }, 'a'), 6);
  assert.equal(delta({ a: 10 }, { a: 4 }, 'a'), null);
  assert.equal(delta({}, {}, 'a'), null);
  const first = { elapsed_ms: 1000, presented_frames: 20, playback_total_frames: 22, playback_dropped_frames: 2,
    rtp: { id: 'video', framesDecoded: 20, framesReceived: 22, bytesReceived: 1000, packetsLost: 0 } };
  const last = { elapsed_ms: 3000, presented_frames: 80, playback_total_frames: 84, playback_dropped_frames: 4,
    rtp: { id: 'video', framesDecoded: 82, framesReceived: 84, bytesReceived: 501000, packetsLost: 1 } };
  const result = webMetrics(first, last);
  assert.equal(result.presented_fps, 30);
  assert.equal(result.decoded_fps, 31);
  assert.equal(result.received_rtp_mbps, 2);
  assert.equal(result.playback_dropped_frames, 2);
  assert.equal(result.playback_drop_percent, 2 / 62 * 100);
  assert.equal(webMetrics(first, { ...last, rtp: { ...last.rtp, id: 'reconnected' } }).decoded_frames, null);
  assert.throws(() => webMetrics(first, first));
  assert.deepEqual(summary([1, 2, 3, 4]), { n: 4, mean: 2.5, p50: 2.5, p95: 4, min: 1, max: 4 });
  assert.equal(summary([]).mean, null);
  console.log('PASS: timed deltas, counter resets, missing values, FPS, bitrate, drops and percentiles');
}
