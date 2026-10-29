const { chromium, devices } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');

const root = path.resolve(__dirname, '../..');
const baseUrl = process.env.THESIS_WEB_URL || 'http://localhost:3001';

(async () => {
  const token = process.env.THESIS_TEST_TOKEN || fs.readFileSync(
    path.join(process.env.HOME, '.config/Diploma/DiplomaFrontendEdge.conf'), 'utf8',
  ).match(/^authToken=(.+)$/m)?.[1];
  assert(token, 'Sign in to Qt first, or set THESIS_TEST_TOKEN');
  const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true });
  const results = [];
  try {
    for (const [name, viewport] of [['desktop', { width: 1440, height: 1000 }], ['mobile', { width: 390, height: 844 }]]) {
      const browserOptions = name === 'mobile' ? devices['Pixel 7'] : {
        userAgent: `Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${browser.version()} Safari/537.36`,
      };
      const page = await browser.newPage({ ...browserOptions, viewport });
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.addInitScript(token => localStorage.setItem('diploma-cloud-auth',
        JSON.stringify({ state: { token, user: null }, version: 0 })), token);
      await page.goto(baseUrl);
      await page.getByRole('button', { name: /^Recordings/ }).click();
      const surface = page.locator('.recorded-viewer').first();
      const video = surface.locator('video');
      await video.waitFor();
      await surface.getByRole('button', { name: 'Mute', exact: true }).click();
      await surface.getByRole('button', { name: 'Play', exact: true }).click();
      await page.waitForFunction(() => {
        const video = document.querySelector('.recorded-viewer video');
        return video && video.videoWidth > 0 && video.currentTime > 0.3;
      }, null, { timeout: 15000 });
      const dimensions = await video.evaluate(video => {
        const canvas = document.createElement('canvas'); canvas.width = 32; canvas.height = 24;
        const context = canvas.getContext('2d'); context.drawImage(video, 0, 0, 32, 24);
        const pixels = context.getImageData(0, 0, 32, 24).data;
        const values = [...pixels].filter((_, i) => i % 4 !== 3);
        return { width: video.videoWidth, height: video.videoHeight, pixelRange: Math.max(...values) - Math.min(...values) };
      });
      assert(dimensions.pixelRange > 5, 'Recording image is blank');
      assert.equal(await video.evaluate(video => video.controls), false, 'Native controls duplicate fullscreen');
      const mediaBounds = await page.locator('.recording-media').first().boundingBox();
      const buttonBounds = await surface.getByRole('button', { name: 'Fullscreen', exact: true }).boundingBox();
      assert(buttonBounds.y >= mediaBounds.y && buttonBounds.y + buttonBounds.height <= mediaBounds.y + mediaBounds.height, 'Fullscreen button is clipped');
      assert(buttonBounds.x >= mediaBounds.x && buttonBounds.x + buttonBounds.width <= mediaBounds.x + mediaBounds.width, 'Fullscreen button escapes the card');
      await page.screenshot({ path: path.join(root, `.runtime/recording-controls-${name}.png`), fullPage: true });
      await surface.getByRole('button', { name: 'Fullscreen', exact: true }).click();
      await page.waitForFunction(() => Boolean(document.fullscreenElement));
      await surface.getByRole('button', { name: 'Exit fullscreen', exact: true }).waitFor();
      assert.equal(await surface.getByRole('button', { name: 'Exit fullscreen', exact: true }).count(), 1);
      const bounds = await surface.boundingBox();
      assert(bounds.width >= viewport.width - 1 && bounds.height >= viewport.height - 1, 'Fullscreen does not fill the screen');
      await page.screenshot({ path: path.join(root, `.runtime/recording-fullscreen-${name}.png`) });
      await surface.getByRole('button', { name: 'Exit fullscreen', exact: true }).click();
      await page.waitForFunction(() => !document.fullscreenElement);
      await surface.getByRole('button', { name: 'Pause', exact: true }).click();
      const seek = surface.getByRole('slider', { name: 'Seek recording' });
      await seek.click();
      await page.waitForFunction(() => {
        const video = document.querySelector('.recorded-viewer video');
        return Math.abs(video.currentTime - video.duration / 2) < 0.5 && video.readyState >= 2;
      });
      // Exercise the in-page fallback used when native fullscreen is unavailable.
      await surface.evaluate(element => { element.requestFullscreen = undefined; });
      await surface.getByRole('button', { name: 'Fullscreen', exact: true }).click();
      await page.waitForFunction(() => Boolean(document.querySelector('.video-expanded')));
      await surface.getByRole('button', { name: 'Exit fullscreen', exact: true }).click();
      await page.waitForFunction(() => !document.querySelector('.video-expanded') && document.body.style.overflow !== 'hidden');
      assert(!await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth), 'Horizontal overflow');
      await page.getByRole('button', { name: /^Live$/ }).click();
      const liveSurface = page.locator('.live-viewer');
      const livePresent = await liveSurface.count() > 0;
      if (livePresent) {
        await page.waitForFunction(() => {
          const video = document.querySelector('.live-viewer video');
          return video && video.videoWidth > 0 && video.getVideoPlaybackQuality().totalVideoFrames > 10;
        }, null, { timeout: 30000 });
        const before = await liveSurface.locator('video').evaluate(video => video.getVideoPlaybackQuality().totalVideoFrames);
        await liveSurface.getByRole('button', { name: 'Fullscreen', exact: true }).click();
        await page.waitForFunction(() => Boolean(document.fullscreenElement));
        await page.screenshot({ path: path.join(root, `.runtime/live-fullscreen-${name}.png`) });
        await liveSurface.getByRole('button', { name: 'Exit fullscreen', exact: true }).click();
        await page.waitForFunction(() => !document.fullscreenElement);
        await page.waitForFunction(before => document.querySelector('.live-viewer video')
          .getVideoPlaybackQuality().totalVideoFrames > before + 10, before);
      }
      assert.deepEqual(errors, [], 'Browser errors');
      results.push({ viewport: name, recording: dimensions, fullscreen: true, seek: true, fallback: true, liveFullscreen: livePresent });
      await page.close();
    }
    fs.writeFileSync(path.join(root, '.runtime/video-controls-result.json'), JSON.stringify(results, null, 2));
    console.log(JSON.stringify(results));
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
