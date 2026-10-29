const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');

(async () => {
  if (!process.env.THESIS_TEST_EMAIL || !process.env.THESIS_TEST_PASSWORD)
    throw new Error('Set THESIS_TEST_EMAIL and THESIS_TEST_PASSWORD');
  const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto('http://localhost:3001');
    await page.getByLabel('Email', { exact: true }).fill(process.env.THESIS_TEST_EMAIL);
    await page.getByLabel('Password', { exact: true }).fill(process.env.THESIS_TEST_PASSWORD);
    await page.getByRole('button', { name: 'Enter Console' }).click();
    await page.getByRole('heading', { name: 'Capture Console', exact: true }).waitFor();
    if (process.env.START_STREAM === '1') {
      await page.getByRole('button', { name: 'Start stream', exact: true }).click();
    }
    await page.waitForFunction(() => {
      const video = document.querySelector('.live-viewer video');
      return video && video.videoWidth > 0 && video.getVideoPlaybackQuality().totalVideoFrames > 10;
    }, null, { timeout: 60000 });
    const first = await page.locator('.live-viewer video').evaluate(video => ({
      width: video.videoWidth, height: video.videoHeight, time: video.currentTime,
      frames: video.getVideoPlaybackQuality().totalVideoFrames,
    }));
    if (process.env.VERIFY_SECONDS) {
      const deadline = Date.now() + Number(process.env.VERIFY_SECONDS) * 1000;
      let previous = first.frames;
      while (Date.now() < deadline) {
        await page.waitForTimeout(5000);
        const frames = await page.locator('.live-viewer video').evaluate(video => video.getVideoPlaybackQuality().totalVideoFrames);
        if (frames <= previous) throw new Error('Live video stopped progressing');
        previous = frames;
      }
    } else {
      await page.waitForFunction(({ frames, additional }) => document.querySelector('.live-viewer video')
        .getVideoPlaybackQuality().totalVideoFrames > frames + additional,
        { frames: first.frames, additional: Number(process.env.VERIFY_FRAMES || 1800) }, { timeout: 120000 });
    }
    const last = await page.locator('.live-viewer video').evaluate(video => {
      const canvas = document.createElement('canvas'); canvas.width = 64; canvas.height = 48;
      const context = canvas.getContext('2d'); context.drawImage(video, 0, 0, 64, 48);
      const pixels = context.getImageData(0, 0, 64, 48).data;
      let min = 255, max = 0;
      for (let i = 0; i < pixels.length; i += 4) { min = Math.min(min, pixels[i]); max = Math.max(max, pixels[i]); }
      return { width: video.videoWidth, height: video.videoHeight, frames: video.getVideoPlaybackQuality().totalVideoFrames, pixelRange: max - min };
    });
    if (last.pixelRange < 5) throw new Error('Cloud camera image is blank');
    await page.screenshot({ path: path.join(root, '.runtime/cloud-camera-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await page.screenshot({ path: path.join(root, '.runtime/cloud-camera-mobile.png'), fullPage: true });
    if (await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth))
      throw new Error('Mobile layout overflows horizontally');
    await page.getByRole('button', { name: /^Recordings/ }).click();
    const recorded = page.locator('.video-list video').first();
    await recorded.waitFor();
    await recorded.evaluate(async video => { video.muted = true; await video.play(); });
    await page.waitForFunction(() => {
      const video = document.querySelector('.video-list video');
      return video && video.videoWidth > 0 && video.currentTime > 0.2;
    });
    const recording = await recorded.evaluate(video => {
      video.pause(); return { width: video.videoWidth, height: video.videoHeight, duration: video.duration };
    });
    await page.screenshot({ path: path.join(root, '.runtime/cloud-recordings-mobile.png'), fullPage: true });
    await page.setViewportSize({ width: 1440, height: 1000 });
    await page.screenshot({ path: path.join(root, '.runtime/cloud-recordings-desktop.png'), fullPage: true });
    for (const name of ['Activity', 'Devices', 'Users']) {
      await page.getByRole('button', { name, exact: true }).click();
      await page.getByRole('heading', { name, exact: true }).waitFor();
      await page.screenshot({ path: path.join(root, `.runtime/cloud-${name.toLowerCase()}.png`), fullPage: true });
    }
    const report = { first, last, recording, errors };
    fs.writeFileSync(path.join(root, '.runtime/cloud-camera-result.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify(report));
    if (errors.length) throw new Error('Browser errors recorded');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
