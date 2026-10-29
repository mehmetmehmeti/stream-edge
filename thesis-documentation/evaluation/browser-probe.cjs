function installBrowserProbe({ token = null, user = null } = {}) {
  if (token) localStorage.setItem('diploma-cloud-auth', JSON.stringify({ state: { token, user }, version: 0 }));
  window.__thesisPeerConnections = new Set();
  const Original = window.RTCPeerConnection;
  window.RTCPeerConnection = new Proxy(Original, { construct(Target, params, newTarget) {
    const pc = Reflect.construct(Target, params, newTarget);
    window.__thesisPeerConnections.add(pc);
    return pc;
  } });
}

function startPresentedCounter() {
  const video = document.querySelector('.live-viewer video');
  if (!video?.requestVideoFrameCallback) throw new Error('requestVideoFrameCallback unavailable');
  const update = (_, metadata) => {
    window.__thesisPresentedFrames = metadata.presentedFrames;
    video.requestVideoFrameCallback(update);
  };
  video.requestVideoFrameCallback(update);
}

function lockMeasurementInput() {
  if (window.__thesisInputLocked) return;
  window.__thesisInputLocked = true;
  window.addEventListener('beforeunload', event => {
    event.preventDefault();
    event.returnValue = '';
  });
  for (const type of ['pointerdown', 'pointerup', 'click', 'dblclick', 'keydown', 'submit']) {
    document.addEventListener(type, event => {
      if (!event.isTrusted) return;
      event.preventDefault();
      event.stopImmediatePropagation();
      console.warn(`Benchmark input blocked: ${type}`);
    }, { capture: true, passive: false });
  }
}

async function browserSnapshot() {
  const video = document.querySelector('.live-viewer video');
  if (!video) throw new Error('Live video element missing');
  const quality = video.getVideoPlaybackQuality();
  let rtp = null;
  for (const pc of window.__thesisPeerConnections) {
    if (pc.connectionState === 'closed') continue;
    const stats = await pc.getStats();
    for (const value of stats.values()) {
      if (value.type !== 'inbound-rtp' || (value.kind || value.mediaType) !== 'video') continue;
      const keys = ['id', 'timestamp', 'bytesReceived', 'headerBytesReceived', 'framesDecoded', 'framesReceived',
        'framesDropped', 'packetsReceived', 'packetsLost', 'jitterBufferDelay', 'jitterBufferEmittedCount',
        'totalDecodeTime', 'freezeCount', 'totalFreezesDuration', 'frameWidth', 'frameHeight'];
      rtp = Object.fromEntries(keys.filter(key => value[key] !== undefined).map(key => [key, value[key]]));
      const codec = stats.get(value.codecId);
      if (codec) rtp.codec = { mimeType: codec.mimeType, sdpFmtpLine: codec.sdpFmtpLine };
    }
  }
  const canvas = document.createElement('canvas'); canvas.width = 64; canvas.height = 48;
  const context = canvas.getContext('2d'); context.drawImage(video, 0, 0, 64, 48);
  const pixels = context.getImageData(0, 0, 64, 48).data;
  let min = 255, max = 0;
  for (let i = 0; i < pixels.length; i += 4) { min = Math.min(min, pixels[i]); max = Math.max(max, pixels[i]); }
  return { elapsed_ms: performance.now(), utc_ms: Date.now(), width: video.videoWidth, height: video.videoHeight,
    presented_frames: window.__thesisPresentedFrames ?? null, playback_total_frames: quality.totalVideoFrames,
    playback_dropped_frames: quality.droppedVideoFrames, pixel_range: max - min, pixel_range_threshold: 5,
    visible: document.visibilityState, rtp };
}

module.exports = { installBrowserProbe, startPresentedCounter, browserSnapshot, lockMeasurementInput };
