const assert = require('node:assert/strict');
const { chromium } = require('playwright');
const { lockMeasurementInput } = require('./browser-probe.cjs');

(async () => {
  const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome', headless: true });
  try {
    const page = await browser.newPage();
    await page.setContent('<button onclick="window.clicked = true">Recordings</button><input aria-label="Value">');
    await page.evaluate(lockMeasurementInput);
    await page.getByRole('button', { name: 'Recordings' }).click();
    assert.equal(await page.evaluate(() => Boolean(window.clicked)), false);
    await page.getByLabel('Value').focus();
    await page.keyboard.type('change');
    assert.equal(await page.getByLabel('Value').inputValue(), '');
    assert.equal(await page.evaluate(() => 40 + 2), 42);
    await page.evaluate(() => { window.reloadSentinel = 42; });
    const dialogPromise = page.waitForEvent('dialog', { timeout: 10000 });
    const reloadPromise = page.reload({ timeout: 10000 }).catch(error => error);
    const dialog = await dialogPromise;
    assert.equal(dialog.type(), 'beforeunload');
    await dialog.dismiss();
    await reloadPromise;
    assert.equal(await page.evaluate(() => window.reloadSentinel), 42);
    console.log('PASS: benchmark blocks clicks, typing and a dismissed reload; browser evaluation remains available.');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
