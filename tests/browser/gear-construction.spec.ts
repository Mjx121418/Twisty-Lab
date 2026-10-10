import { test, expect } from '@playwright/test';

test('shows 38 independent pieces and the six gears of each parent group', async ({ page }) => {
  await page.goto('./gear-construction.html');
  const view = page.locator('#view');
  await expect(view).toHaveAttribute('data-piece-count', '38');
  await expect(view).toHaveAttribute('data-visible-pieces', '38');
  await expect(view).not.toHaveAttribute('data-error', 'true');
  await page.getByLabel('Piece family').selectOption('extra-edge');
  await expect(view).toHaveAttribute('data-visible-pieces', '12');
  await page.getByLabel('Isolate').selectOption('extra-edge/UF');
  await expect(view).toHaveAttribute('data-visible-pieces', '1');
  await expect(page.getByRole('status')).toContainText('extra-edge/UF');
  await view.screenshot({ path: 'test-results/gear-compound-extra-edge.png' });
  await page.getByLabel('Isolate').selectOption('all');
  await page.getByLabel('Piece family').selectOption('all');
  await view.screenshot({ path: 'test-results/gear-compound-assembly.png' });
  await page.getByRole('combobox', { name: 'Show', exact: true }).selectOption('drivers');
  await expect(view).toHaveAttribute('data-piece-count', '18');
  for (const group of ['UD', 'RL', 'FB']) {
    await page.getByLabel('Gear group').selectOption(group);
    await expect(view).toHaveAttribute('data-visible-pieces', '6');
    await expect(view).not.toHaveAttribute('data-error', 'true');
  }
  await view.screenshot({ path: 'test-results/gear-compound-parent-group.png' });
  await page.getByRole('combobox', { name: 'Show', exact: true }).selectOption('pieces');
  await expect(view).toHaveAttribute('data-visible-pieces', '38');
});
