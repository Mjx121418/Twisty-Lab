import { expect, test, type Page } from '@playwright/test';

async function resting(page: Page): Promise<void> {
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const digest = await page.getByTestId('state-digest').textContent();
  for (const id of ['cube-view', 'diagram-view'])
    await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest', digest!);
}

for (const [puzzle, move, next] of [['cube3', 'R', 'U'], ['flat-hex-klein-bottle', 'A', 'B']]) {
  test(`reset restores ${puzzle}, clears undo/redo and keeps both views interactive`, async ({ page }) => {
    test.setTimeout(60_000);
    await page.goto('./');
    await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
    if (puzzle !== 'cube3') await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle);
    await resting(page);
    await page.getByLabel('Animation speed').selectOption('0');
    const origin = await page.getByTestId('state-digest').textContent();
    const notation = `${move} ${next}`;
    await page.getByLabel('Algorithm', { exact: true }).fill(notation);
    await page.locator('.piece-list button').first().click();
    await page.getByRole('button', { name: `Move ${move}`, exact: true }).click();
    await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
    await resting(page);

    const reset = page.getByRole('button', { name: 'Reset puzzle', exact: true });
    await reset.click();
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
    await resting(page);
    await expect(page.getByLabel('Puzzle', { exact: true })).toHaveValue(puzzle);
    await expect(page.getByLabel('Algorithm', { exact: true })).toHaveValue(notation);
    await expect(page.getByRole('button', { name: '↶ Undo' })).toBeDisabled();
    await expect(page.getByRole('button', { name: '↷ Redo' })).toBeDisabled();
    for (const id of ['cube-view', 'diagram-view'])
      await expect(page.getByTestId(id)).toHaveAttribute('data-selected-piece', '');

    // Exercise fresh-session geometry and reset a solved state with redo history.
    await page.getByRole('button', { name: `Move ${next}`, exact: true }).click();
    await expect(page.getByTestId('state-digest')).not.toHaveText(origin!);
    await resting(page);
    await page.getByRole('button', { name: '↶ Undo' }).click();
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
    await resting(page);
    await expect(page.getByRole('button', { name: '↷ Redo' })).toBeEnabled();
    await reset.click();
    await resting(page);
    await expect(page.getByRole('button', { name: '↷ Redo' })).toBeDisabled();
    await page.getByRole('button', { name: 'History', exact: true }).click();
    await expect(page.locator('.history-list')).toContainText('0 committed primitives');
    await expect(page.getByRole('alert')).toHaveCount(0);
  });
}
