import { expect, test } from '@playwright/test';
import { expectViews, setViews } from './view-controls';

for (const family of ['a', 'b', 'c', 'd']) {
  test(`Melty ${family.toUpperCase()} defaults to synchronized cube and sphere with an optional diagram`, async ({ page }) => {
    test.setTimeout(90_000);
    const errors: string[] = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto('./');
    await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
    await page.getByLabel('Puzzle', { exact: true }).selectOption(`melty-${family}`);
    await expectViews(page, ['cube', 'sphere']);
    const cube = page.locator('[data-view-id="cube"]');
    const sphere = page.locator('[data-view-id="sphere"]');
    await expect(cube).toHaveAttribute('data-realization-id', `melty-${family}-euclidean-v1`);
    await expect(sphere).toHaveAttribute('data-realization-id', `melty-${family}-spherical-v1`);
    await expect(sphere.getByLabel('Spherical puzzle')).toBeVisible();
    const initial = await page.getByTestId('state-digest').textContent();
    await page.getByLabel('Animation speed').selectOption('0');
    await page.getByText('Advanced movement list', { exact: true }).click();
    await page.getByRole('button', { name: 'Move U', exact: true }).click();
    await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
    await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
    const after = await page.getByTestId('state-digest').textContent();
    for (const view of [cube, sphere]) await expect(view).toHaveAttribute('data-state-digest', after!);
    await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
    await expect(page.getByTestId('state-digest')).toHaveText(initial!);
    await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
    await page.screenshot({ path: `test-results/melty-${family}-cube-sphere.png`, fullPage: true });
    await setViews(page, ['cube', 'sphere', 'diagram']);
    await expect(page.locator('.views canvas')).toHaveCount(3);
    await expect(page.locator('[data-view-id="diagram"]')).toHaveAttribute('data-realization-id', `melty-${family}-port-diagram-v1`);
    for (const view of await page.locator('.views .canvas-host').all()) await expect(view).toHaveAttribute('data-state-digest', initial!);
    await setViews(page, ['sphere']);
    await expect(page.locator('.views canvas')).toHaveCount(1);
    await page.locator('.view-picker summary').click();
    await page.getByRole('button', { name: 'Restore default views', exact: true }).click();
    await page.locator('.view-picker summary').click();
    await expectViews(page, ['cube', 'sphere']);
    await expect(page.getByRole('alert')).toHaveCount(0);
    expect(errors).toEqual([]);
  });
}
