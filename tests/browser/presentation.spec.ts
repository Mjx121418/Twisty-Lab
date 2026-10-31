import { expect, test } from '@playwright/test';

test('authored movement labels change buttons while request history keeps its identity', async ({ page }) => {
  let renamed = false;
  await page.route(/\/presentation-[^/]+\.json$/, async route => {
    const response = await route.fetch();
    const document = await response.json();
    if (document.kind === 'twisty-presentation' && document.puzzleId === 'cube3') {
      document.display.operations.R.label = 'Right cap';
      document.display.operations.R.accessibleLabel = 'Turn right cap';
      document.display.operations.R.aliases = ['right cap'];
      renamed = true;
      await route.fulfill({ response, json: document });
    } else await route.fulfill({ response });
  });
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  expect(renamed).toBe(true);
  await page.getByLabel('Animation speed').selectOption('0');
  const initial = await page.getByTestId('state-digest').textContent();
  const movement = page.getByRole('button', { name: 'Move Turn right cap', exact: true });
  await expect(movement).toHaveText('Right cap');
  await movement.click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗', exact: true }).click();
  const stream = await (await downloading).createReadStream();
  const chunks: Buffer[] = [];
  for await (const chunk of stream!) chunks.push(chunk);
  const saved = JSON.parse(Buffer.concat(chunks).toString('utf8'));
  expect(saved.logical.requests[0].operation).toBe('R');
  await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
});
