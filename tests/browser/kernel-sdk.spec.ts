import { execFileSync } from 'node:child_process';
import { expect, test } from '@playwright/test';

test('a separate Canvas renderer and controller consume only the portable kernel', async ({ page, baseURL }) => {
  const modules: string[] = [];
  page.on('request', (request) => { if (/\.(js|mjs)(\?|$)/.test(request.url())) modules.push(request.url()); });
  await page.goto('kernel/examples/canvas/');
  await expect(page.getByRole('status')).toHaveText('Ready');
  await expect(page.locator('canvas')).toHaveCount(1);
  const initial = await page.locator('#digest').textContent();
  const canvas = page.getByLabel('Canvas port diagram');
  // The front-face center in the C++ net is at (0,0), offset from its chart center.
  const center = await canvas.evaluate((element: HTMLCanvasElement) => ({
    x: (0.5 - 1.5 * Math.min(element.width / 13, element.height / 10) / element.width) * element.clientWidth,
    y: element.clientHeight / 2,
  }));
  await canvas.click({ position: center });
  await expect(page.locator('#selection')).toContainText('Selected center/F');
  await page.getByRole('button', { name: 'U', exact: true }).click();
  await expect(page.locator('#digest')).not.toHaveText(initial!);
  await expect(page.getByRole('button', { name: 'Undo', exact: true })).toBeEnabled();
  await page.getByRole('button', { name: 'Undo', exact: true }).click();
  await expect(page.locator('#digest')).toHaveText(initial!);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('helicopter');
  await expect(page.getByRole('button', { name: 'UF_ab', exact: true })).toBeVisible();
  await page.getByLabel('Algorithm', { exact: true }).fill('UF_ab UL_af');
  await page.getByRole('button', { name: 'Play algorithm' }).click();
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/helicopter/definition.json', '--algorithm', 'UF_ab UL_af', '--json'], { encoding: 'utf8' }));
  await expect(page.locator('#digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByRole('button', { name: 'UR_ad', exact: true }).click();
  await expect(page.getByRole('status')).toHaveText('placement.blocked');
  await expect(page.locator('#digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByLabel('Algorithm', { exact: true }).fill("(UF_ab UL_af)'");
  await page.getByRole('button', { name: 'Play algorithm' }).click();
  const origin = JSON.parse(execFileSync('build/native/twisty', ['inspect', '--definition', 'packages/helicopter/definition.json', '--json'], { encoding: 'utf8' }));
  await expect(page.locator('#digest')).toHaveText(origin.snapshot.stateDigest);
  expect(modules.some((url) => url.endsWith('/kernel/index.js'))).toBe(true);
  const kernelPath = new URL('kernel/', baseURL!).pathname;
  expect(modules.every((url) => new URL(url).pathname.startsWith(kernelPath))).toBe(true);
  await page.screenshot({ path: 'test-results/kernel-canvas.png', fullPage: true });
});
