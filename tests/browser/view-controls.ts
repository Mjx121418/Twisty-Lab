import { expect, type Page } from '@playwright/test';
import { viewLabels, type ViewId } from '../../web/src/viewSelection';

const pairs: Record<string, ViewId[]> = {
  'cube-sphere': ['cube', 'sphere'], 'cube-diagram': ['cube', 'diagram'],
  'sphere-diagram': ['sphere', 'diagram'], 'square-tiles': ['fundamental-domain', 'tiled-plane'],
};

export async function expectViews(page: Page, selection: string | ViewId[]): Promise<void> {
  const ids = typeof selection === 'string' ? pairs[selection] : selection;
  await expect.poll(() => page.locator('.views > article').evaluateAll(nodes => nodes.map(node => node.getAttribute('data-view')))).toEqual(ids);
}

export async function setViews(page: Page, selection: string | ViewId[]): Promise<void> {
  const ids = typeof selection === 'string' ? pairs[selection] : selection;
  const picker = page.locator('.view-picker');
  if (await picker.getAttribute('open') === null) await picker.locator('summary').click();
  for (const id of ids) await picker.getByRole('checkbox', { name: viewLabels[id], exact: true }).check();
  for (const checkbox of await picker.getByRole('checkbox').all()) {
    const name = await checkbox.locator('..').textContent();
    if (!ids.some(id => viewLabels[id] === name)) await checkbox.uncheck();
  }
  for (const [index, id] of ids.entries()) {
    while ((await picker.locator('li').evaluateAll(nodes => nodes.map(node => node.getAttribute('data-view-option')))).indexOf(id) > index) {
      await picker.getByRole('button', { name: `Move ${viewLabels[id]} earlier`, exact: true }).click();
    }
  }
  await picker.locator('summary').click();
  await expectViews(page, ids);
}
