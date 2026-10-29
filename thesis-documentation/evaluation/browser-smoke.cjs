// Synthetic loopback validates the browser sampler; it is NOT thesis camera evidence.
const assert = require('node:assert/strict');
const { chromium } = require('playwright');
const { installBrowserProbe, startPresentedCounter, browserSnapshot } = require('./browser-probe.cjs');
const { webMetrics } = require('./metrics.cjs');

(async () => {
  const server = await chromium.launchServer({ executablePath: '/usr/bin/google-chrome', headless: true });
  let browser;
  try {
    browser = await chromium.connect(server.wsEndpoint());
    assert(server.process().pid > 0);
    const page = await browser.newPage();
    await page.addInitScript(installBrowserProbe, {});
    await page.goto('about:blank');
    await page.setContent('<div class="live-viewer"><video muted autoplay playsinline></video></div>');
    await page.evaluate(async () => {
      const canvas = document.createElement('canvas'); canvas.width = 320; canvas.height = 240;
      const context = canvas.getContext('2d');
      let n = 0;
      setInterval(() => {
        context.fillStyle = '#000'; context.fillRect(0, 0, 320, 240);
        context.fillStyle = '#fff'; context.fillRect((n++ % 200), 0, 100, 240);
      }, 33);
      const source = canvas.captureStream(30);
      const a = new RTCPeerConnection(), b = new RTCPeerConnection();
      a.onicecandidate = event => { if (event.candidate) b.addIceCandidate(event.candidate).catch(() => {}); };
      b.onicecandidate = event => { if (event.candidate) a.addIceCandidate(event.candidate).catch(() => {}); };
      b.ontrack = event => {
        const video = document.querySelector('video'); video.srcObject = new MediaStream([event.track]);
        video.play().catch(() => {});
      };
      a.addTrack(source.getVideoTracks()[0], source);
      await a.setLocalDescription(await a.createOffer());
      await b.setRemoteDescription(a.localDescription);
      await b.setLocalDescription(await b.createAnswer());
      await a.setRemoteDescription(b.localDescription);
    });
    await page.waitForFunction(() => document.querySelector('video').getVideoPlaybackQuality().totalVideoFrames > 10);
    await page.evaluate(startPresentedCounter);
    await page.waitForTimeout(1000);
    const first = await page.evaluate(browserSnapshot);
    await page.waitForTimeout(2000);
    const last = await page.evaluate(browserSnapshot);
    const metrics = webMetrics(first, last);
    assert(metrics.presented_frames > 0);
    assert(metrics.decoded_frames > 0);
    assert(metrics.received_rtp_mbps > 0);
    assert(last.pixel_range >= 5);
    assert.equal(last.pixel_range_threshold, 5);
    console.log('PASS: Chrome synthetic WebRTC loopback validates timed presentation, decoding, RTP bitrate and pixel threshold. No camera used.');
  } finally {
    await browser?.close(); await server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
