import { expect, type Page } from '@playwright/test';
import type { ViewId } from '../../web/src/viewSelection';

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
  for (const id of ids) await picker.locator(`[data-view-option="${id}"]`).getByRole('checkbox').check();
  for (const row of await picker.locator('[data-view-option]').all()) {
    const id = await row.getAttribute('data-view-option') as ViewId;
    if (!ids.includes(id)) await row.getByRole('checkbox').uncheck();
  }
  for (const [index, id] of ids.entries()) {
    while ((await picker.locator('li').evaluateAll(nodes => nodes.map(node => node.getAttribute('data-view-option')))).indexOf(id) > index) {
      await picker.locator(`[data-view-option="${id}"]`).getByRole('button', { name: / earlier$/ }).click();
    }
  }
  await picker.locator('summary').click();
  await expectViews(page, ids);
}
