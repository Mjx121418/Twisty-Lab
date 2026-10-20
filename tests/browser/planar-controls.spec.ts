import { expect, test, type Page } from '@playwright/test';

async function nextFrame(page: Page): Promise<void> {
  await page.evaluate(() => new Promise<void>(resolve => requestAnimationFrame(() => resolve())));
}

for (const puzzle of ['flat-torus', 'flat-hex-klein-bottle']) {
  test(`${puzzle}: locks domain zoom and fills the tiled panel without a polygon mask`, async ({ page }) => {
    test.setTimeout(60_000);
    const errors:string[]=[];page.on('pageerror',error=>errors.push(error.message));
    await page.goto('./');
    await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
    await page.getByLabel('Puzzle', { exact: true }).selectOption(puzzle);
    const domain=page.getByTestId('cube-view'),plane=page.getByTestId('diagram-view');
    await expect(domain).toHaveAttribute('data-topology',puzzle.includes('klein')?'klein-bottle':'torus');
    const origin=await page.getByTestId('state-digest').textContent();
    const outline=domain.locator('.surface-viewport polygon'),points=await outline.getAttribute('points');
    await domain.locator('canvas').hover();await page.mouse.wheel(0,-500);await nextFrame(page);
    await expect(domain).toHaveAttribute('data-camera-zoom','1');
    await page.mouse.wheel(0,500);await nextFrame(page);
    await expect(domain).toHaveAttribute('data-camera-zoom','1');
    await expect(outline).toHaveAttribute('points',points!);

    await expect(plane.locator('.surface-viewport')).toHaveCount(0);
    const canvas=plane.locator('canvas');
    // This corner lay outside the old square/rhombus mask. The plane must now
    // render a pickable piece there, with no secondary clipping boundary.
    await canvas.click({position:{x:12,y:15}});
    await expect(plane).toHaveAttribute('data-selected-piece',/^(face|edge|vertex)-/);
    await page.keyboard.press('Escape');
    await expect(plane).toHaveAttribute('data-selected-piece','');
    await page.getByRole('button',{name:'Tiled plane',exact:true}).click();
    await canvas.hover();await page.mouse.wheel(0,1000);
    await expect.poll(async()=>Number(await plane.getAttribute('data-camera-zoom'))).toBeLessThan(1);
    await canvas.click({position:{x:12,y:15}});
    await expect(plane).toHaveAttribute('data-selected-piece',/^(face|edge|vertex)-/);
    await expect(page.getByTestId('state-digest')).toHaveText(origin!);
    await expect(page.getByRole('alert')).toHaveCount(0);expect(errors).toEqual([]);
    await page.screenshot({path:`test-results/${puzzle}-unclipped-plane.png`,fullPage:true});
  });
}
