import { expect, test } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

test('Helicopter destinations track six phases, queued intents, undo and saved state', async ({ page }) => {
  test.setTimeout(90_000);
  const errors: string[] = []; page.on('pageerror', e => errors.push(e.message));
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('helicopter');
  await page.getByLabel('Twist family', { exact: true }).selectOption('UF');
  await expect(page.getByTestId('current-stop-UF')).toHaveText('Current: a');
  await expect(page.getByRole('button', { name: 'Move UF_ab', exact: true })).toBeHidden();
  await page.getByLabel('Animation speed').selectOption('450');
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: /^UF destination d/ }).click();
  await expect(page.getByTestId('current-stop-UF')).toHaveText('Current: d');
  await page.getByRole('button', { name: /^UF destination a/ }).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await expect(page.getByTestId('current-stop-UF')).toHaveText('Current: a');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('current-stop-UF')).toHaveText('Current: d');
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = await (await downloading).path();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await page.getByRole('button', { name: '↷ Redo', exact: true }).click();
  await expect(page.getByTestId('current-stop-UF')).toHaveText('Current: a');
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await page.getByLabel('Open session file').setInputFiles(saved!);
  await expect(page.getByTestId('current-stop-UF')).toHaveText('Current: d');
  await page.getByRole('button', { name: /^UF destination d/ }).focus();
  const revision = await page.locator('.revision').textContent();
  await page.keyboard.press('ArrowRight');
  await expect(page.getByRole('button', { name: /^UF destination e/ })).toBeFocused();
  expect(await page.locator('.revision').textContent()).toBe(revision);
  await page.screenshot({ path: 'test-results/helicopter-family-controls.png', fullPage: true });
  expect(errors).toEqual([]);
});

test('Melty destination controls allow independent opposite caps and separate history', async ({ page }) => {
  test.setTimeout(120_000);
  const fixtures = JSON.parse(readFileSync('tests/fixtures/core.json', 'utf8')).cases;
  const fixture = fixtures.find((f: { name: string }) => f.name === 'Melty B opposite caps execute separately');
  const pkg = JSON.parse(readFileSync(fixture.source, 'utf8'));
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', fixture.source,
    '--algorithm', `${fixture.prefix} ${fixture.operation}`, '--policy', 'transactional', '--json'], { encoding: 'utf8' }));
  const prefix = fixture.prefix.split(' ');
  const first = prefix.pop()!;
  const steps = [first, fixture.operation];
  const transitions = reference.transitions.slice(-2);
  const targets = steps.map((op: string, index: number) => {
    const id = pkg.catalog.requests[op].family;
    const driver = pkg.catalog.controls.drivers.find((d: { id: string }) => d.id === id);
    return { id, stop: driver.placements[transitions[index].afterState.placementOf[driver.piece]] };
  });
  expect(pkg.catalog.controls.families).toHaveLength(6);
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('melty-b');
  await expect(page.getByLabel('Twist family', { exact: true })).toBeVisible();
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByLabel('Algorithm', { exact: true }).fill(prefix.join(' '));
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const before = await page.getByTestId('state-digest').textContent();
  let intermediate: string | null = null;
  for (const { id, stop } of targets) {
    await page.getByLabel('Twist family', { exact: true }).selectOption(id);
    await page.getByRole('button', { name: new RegExp(`^${id} destination ${stop}(?:,|$)`) }).click();
    await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
    if (!intermediate) {
      await expect(page.getByTestId('state-digest')).not.toHaveText(before!);
      intermediate = await page.getByTestId('state-digest').textContent();
    }
  }
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(intermediate!);
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(before!);
  await expect(page.getByRole('button', { name: 'Apply paired turn', exact: true })).toHaveCount(0);
  await expect(page.getByRole('alert')).toHaveCount(0);
  await page.screenshot({ path: 'test-results/melty-independent-controls.png', fullPage: true });
});
