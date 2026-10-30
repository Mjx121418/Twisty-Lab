import { readFile } from 'node:fs/promises';
import { expect, test } from '@playwright/test';
test('S³ uses one view, all cell-axis categories, reversible turns, and presentation-only focus', async ({ page }) => {
  test.setTimeout(60_000);
  const errors: string[] = []; page.on('pageerror', e => errors.push(e.message));
  await page.goto('./');
  await expect(page.getByTestId('goal-status')).toHaveText('Solved');
  await page.getByLabel('Puzzle', { exact: true }).selectOption('tesseract3');
  const controls = page.getByRole('region', { name: 'Cell twist controller' });
  await expect(controls).toBeVisible();
  await expect(page.locator('canvas')).toHaveCount(1);
  await page.locator('canvas').screenshot({ path: 'test-results/s3-outer-opacity.png' });
  await expect(page.getByRole('group', { name: 'Eight cells' }).getByRole('button')).toHaveCount(8);
  await expect(page.getByRole('group', { name: 'Axis kind' }).getByRole('button')).toHaveCount(3);
  await page.getByLabel('Animation speed').selectOption('0');
  const digest = await page.getByTestId('state-digest').textContent();
  for (const [category, count, angle] of [['Face axes', 3, '+90°'], ['Edge axes', 6, '+180°'], ['Vertex axes', 4, '+120°']] as const) {
    await controls.getByRole('button', { name: category, exact: true }).click();
    await expect(page.getByLabel('Rotation axis').locator('option')).toHaveCount(count);
    await controls.getByRole('button', { name: angle, exact: true }).click();
    await expect(page.getByTestId('state-digest')).not.toHaveText(digest!);
    await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
    await page.getByRole('button', { name: '↶ Undo', exact: true }).click();
    await expect(page.getByTestId('state-digest')).toHaveText(digest!);
  }
  await page.getByRole('group', { name: 'Eight cells' }).getByRole('button', { name: '−W', exact: true }).click();
  await controls.getByRole('button', { name: 'Focus −W cell' }).click();
  await expect(page.getByTestId('state-digest')).toHaveText(digest!);
  await page.getByRole('button', { name: 'Toggle outer layers' }).click();
  await expect(page.getByRole('alert')).toHaveCount(0);
  await page.getByRole('button', { name: 'Select center/Xp', exact: true }).click();
  await expect(controls).toHaveAttribute('data-matching-cells', 'Xp');
  await controls.getByRole('button', { name: 'Clear piece selection' }).click();
  const fade = controls.getByRole('slider', {name:'Fade start radius'});
  await fade.press('End');
  await expect(fade).toHaveValue('5.53');
  const fadeEnd = controls.getByRole('slider', {name:'Fade end radius'});
  await expect(fadeEnd).toHaveValue('5.83');
  await fadeEnd.press('ArrowLeft');
  await expect(fadeEnd).toHaveValue('5.82');
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const file = await (await download).path();
  const saved = JSON.parse(await readFile(file!, 'utf8'));
  expect(saved.presentation.viewOrder).toEqual(['stereographic']);
  expect(saved.presentation.cameras[0].projection.focusCell).toBe('Wm');
  expect(saved.presentation.cameras[0].shellVisible).toBe(false);
  expect(saved.presentation.cameras[0].fadeStartRadius).toBe(5.53);
  expect(saved.presentation.cameras[0].fadeEndRadius).toBe(5.82);
  await fadeEnd.press('Home');
  await expect(fadeEnd).toHaveValue('0.72');
  await fade.press('Home');
  await expect(fade).toHaveValue('0.71');
  await page.getByRole('button', { name: '↷ Redo', exact: true }).click();
  await expect(page.getByTestId('state-digest')).not.toHaveText(digest!);
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await page.getByLabel('Open session file').setInputFiles(file!);
  await expect(page.getByTestId('state-digest')).toHaveText(digest!);
  await expect(fade).toHaveValue('5.53');
  await expect(fadeEnd).toHaveValue('5.82');
  await expect(page.getByRole('alert')).toHaveCount(0);
  expect(errors).toEqual([]);
});

test('S³ animates on the GPU without per-frame drawing rebuilds in every cell focus', async ({ page }) => {
  test.setTimeout(90_000);
  const errors: string[] = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  await page.goto('./');
  await expect(page.getByTestId('goal-status')).toHaveText('Solved');
  await page.getByLabel('Puzzle', { exact: true }).selectOption('tesseract3');
  const controls = page.getByRole('region', { name: 'Cell twist controller' });
  const host = page.locator('[data-view-id="stereographic"]');
  await expect(host).toHaveAttribute('data-vertex-transform', 'gpu');
  await page.getByLabel('Animation speed').selectOption('3000');
  for (const cell of ['+X', '−X', '+Y', '−Y', '+Z', '−Z', '+W', '−W']) {
    await page.getByRole('group', { name: 'Eight cells' }).getByRole('button', { name: cell, exact: true }).click();
    await controls.getByRole('button', { name: `Focus ${cell} cell`, exact: true }).click();
    const revision = Number(await host.getAttribute('data-drawing-revision'));
    const frame = Number(await host.getAttribute('data-frame-id'));
    const before = cell === '+X' ? await host.locator('canvas').screenshot() : undefined;
    await controls.getByRole('button', { name: '+90°', exact: true }).click();
    await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'true');
    await expect.poll(async () => Number(await host.getAttribute('data-frame-id'))).toBeGreaterThan(frame + 3);
    expect(Number(await host.getAttribute('data-drawing-revision'))).toBe(revision);
    if (before) {
      const middle = await host.locator('canvas').screenshot({ path: 'test-results/s3-gpu-mid-turn.png' });
      expect(middle.equals(before)).toBe(false);
    }
    await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
    // The exact committed pose is installed without rebuilding the CPU drawing.
    expect(Number(await host.getAttribute('data-drawing-revision'))).toBe(revision);
  }
  expect(errors).toEqual([]);
});

test('S³ canvas picking agrees with the CPU drawing after a GPU turn and focus change', async ({ page }) => {
  const { readFileSync } = await import('node:fs');
  const THREE = await import('three');
  const { createKernel, KernelSession, GeometryInstance } = await import('../../kernel/index');
  await page.goto('./');
  await expect(page.getByTestId('goal-status')).toHaveText('Solved');
  await page.getByLabel('Puzzle', { exact: true }).selectOption('tesseract3');
  const controls = page.getByRole('region', { name: 'Cell twist controller' });
  await expect(controls).toBeVisible();
  await page.getByLabel('Animation speed').selectOption('0');
  await page.getByLabel('Rotation axis').selectOption('fY');
  await controls.getByRole('button', { name: '+90°', exact: true }).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  await controls.getByRole('button', { name: 'Focus +X cell', exact: true }).click();
  const host = page.locator('[data-view-id="stereographic"]');
  await host.scrollIntoViewIfNeeded();
  const bounds = (await host.locator('canvas').boundingBox())!;
  const runtime = await createKernel();
  const session = new KernelSession(runtime, readFileSync('packages/tesseract3/package.json', 'utf8'));
  session.lightweightResults(); session.move('Xp_fY');
  await expect(page.getByTestId('state-digest')).toHaveText(session.stateSnapshot().stateDigest);
  const geometry = new GeometryInstance(runtime, session.native, readFileSync('packages/tesseract3/tesseract3-s3.json', 'utf8'));
  const material = new THREE.MeshBasicMaterial({ side: THREE.DoubleSide });
  const assets = new Map<string, InstanceType<typeof THREE.BufferGeometry>>();
  try {
    geometry.setProjection({ focusCell: 'Xp' });
    for (const a of geometry.scene.meshAssets) {
      const mesh = new THREE.BufferGeometry();
      mesh.setAttribute('position', new THREE.BufferAttribute(geometry.positions.slice(a.positionOffset, a.positionOffset + 3 * a.vertexCount), 3));
      mesh.setIndex(new THREE.BufferAttribute(geometry.indices.slice(a.indexOffset, a.indexOffset + a.indexCount), 1));
      assets.set(a.id, mesh);
    }
    const meshes = geometry.scene.visualParts.filter(p => geometry.scene.projection!.pieceDepths[p.pieceId] <= -.49).map(p => {
      const mesh = new THREE.Mesh(assets.get(p.meshAssetId), material);
      mesh.userData.pieceId = p.pieceId; mesh.updateMatrixWorld(); return mesh;
    });
    const camera = new THREE.PerspectiveCamera(35, bounds.width / bounds.height, .1, 100);
    camera.position.set(5, 4.2, 6).multiplyScalar(.62); camera.lookAt(0, 0, 0); camera.updateMatrixWorld();
    const ray = new THREE.Raycaster();
    const at = (x: number, y: number) => {
      ray.setFromCamera(new THREE.Vector2(x, y), camera);
      return ray.intersectObjects(meshes)[0]?.object.userData.pieceId as string | undefined;
    };
    let pick: { x: number; y: number; piece: string } | undefined;
    for (const x of [.04, -.04, .08, -.08, .12, -.12, .16, -.16, 0]) {
      for (const y of [.1, -.1, .2, -.2, .3, -.3, .4, -.4, 0]) {
        const piece = at(x, y);
        const margin = [[-3, 0], [3, 0], [0, -3], [0, 3]];
        if (piece && !piece.startsWith('center/') && margin.every(([dx, dy]) => at(x + 2 * dx / bounds.width, y + 2 * dy / bounds.height) === piece)) {
          pick = { x, y, piece }; break;
        }
      }
      if (pick) break;
    }
    expect(pick).toBeDefined();
    await host.locator('canvas').click({ position: { x: (pick!.x + 1) * bounds.width / 2, y: (1 - pick!.y) * bounds.height / 2 } });
    await expect(host).toHaveAttribute('data-selected-piece', pick!.piece);
    await expect(page.getByRole('alert')).toHaveCount(0);
  } finally {
    for (const asset of assets.values()) asset.dispose(); material.dispose(); geometry.dispose(); session.dispose();
  }
});
