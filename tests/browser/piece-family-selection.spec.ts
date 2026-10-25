import { test, expect, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { BufferAttribute, BufferGeometry, DoubleSide, FrontSide, Mesh, MeshBasicMaterial, OrthographicCamera, PerspectiveCamera, Raycaster, Vector2, Vector3 } from 'three';
import { createKernel, GeometryInstance, KernelSession, type MainModule } from '../../kernel/index';
import { setViews } from './view-controls';

let runtime: MainModule;
test.beforeAll(async () => { runtime = await createKernel(); });

// Project a visible triangle from the public interpreter frame, then send a
// real canvas click/tap. No test hooks or frontend scene internals are needed.
async function pick(page: Page, view: string, realization: string, piece: string, touch = false) {
  const host = page.locator(`[data-view-id="${view}"]`);
  await host.scrollIntoViewIfNeeded();
  const bounds = (await host.boundingBox())!;
  const session = new KernelSession(runtime, readFileSync('packages/helicopter/package.json', 'utf8'));
  const geometry = new GeometryInstance(runtime, session.native, readFileSync(`packages/helicopter/${realization}.json`, 'utf8'));
  const assets = new Map<string, BufferGeometry>();
  const material = new MeshBasicMaterial({ side: geometry.scene.diagram ? DoubleSide : FrontSide });
  try {
    const scene = geometry.scene;
    const positions = new Float32Array(geometry.definition.positions());
    const indices = new Uint32Array(geometry.definition.indices());
    for (const asset of scene.meshAssets) {
      const mesh = new BufferGeometry();
      mesh.setAttribute('position', new BufferAttribute(positions.slice(asset.positionOffset, asset.positionOffset + asset.vertexCount * 3), 3));
      mesh.setIndex(new BufferAttribute(indices.slice(asset.indexOffset, asset.indexOffset + asset.indexCount), 1));
      assets.set(asset.id, mesh);
    }
    const camera = scene.diagram ? new OrthographicCamera() : new PerspectiveCamera(35, bounds.width / bounds.height, .1, 100);
    if (camera instanceof OrthographicCamera) {
      const horizontal = Math.max(6.5, 5.1 * bounds.width / bounds.height);
      const vertical = horizontal * bounds.height / bounds.width;
      Object.assign(camera, { left: -horizontal, right: horizontal, top: vertical, bottom: -vertical, near: .1, far: 100 });
      camera.position.set(1.5, 0, 15); camera.lookAt(1.5, 0, 0);
    } else { camera.position.set(5, 4.2, 6); camera.lookAt(0, 0, 0); }
    camera.updateProjectionMatrix(); camera.updateMatrixWorld(true);
    const meshes = scene.visualParts.map((part, i) => {
      const mesh = new Mesh(assets.get(part.meshAssetId), material);
      mesh.matrixAutoUpdate = false; mesh.matrix.fromArray(geometry.matrices, i * 16);
      mesh.updateMatrixWorld(true); mesh.userData.piece = part.pieceId;
      return mesh;
    });
    const ray = new Raycaster();
    let point: Vector3 | undefined;
    for (const mesh of meshes.filter(mesh => mesh.userData.piece === piece)) {
      const vertices = mesh.geometry.getAttribute('position'), triangles = mesh.geometry.getIndex()!;
      for (let i = 0; i < triangles.count; i += 3) {
        const center = new Vector3();
        for (let j = 0; j < 3; j++) center.add(new Vector3().fromBufferAttribute(vertices, triangles.getX(i + j)));
        center.multiplyScalar(1 / 3).applyMatrix4(mesh.matrix).project(camera);
        if (Math.abs(center.x) > .9 || Math.abs(center.y) > .9) continue;
        ray.setFromCamera(new Vector2(center.x, center.y), camera);
        if (ray.intersectObjects(meshes)[0]?.object.userData.piece === piece) { point = center; break; }
      }
      if (point) break;
    }
    expect(point, `Visible ${piece} in ${view}`).toBeDefined();
    const x = (point!.x + 1) * bounds.width / 2, y = (1 - point!.y) * bounds.height / 2;
    if (touch) await page.touchscreen.tap(bounds.x + x, bounds.y + y);
    else await host.locator('canvas').click({ position: { x, y } });
  } finally { for (const asset of assets.values()) asset.dispose(); material.dispose(); geometry.dispose(); session.dispose(); }
}

async function open(page: Page) {
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/^[0-9a-f]{64}$/);
  await page.getByLabel('Puzzle', { exact: true }).selectOption('helicopter');
  await expect(page.getByLabel('Twist family', { exact: true })).toBeVisible();
}

test('canvas picks share multi-selection across cube, sphere and diagram; dragging does not select', async ({ page }) => {
  test.setTimeout(90_000);
  const errors: string[] = []; page.on('pageerror', error => errors.push(error.message));
  await open(page);
  const initial = await page.getByTestId('state-digest').textContent();
  await pick(page, 'cube', 'helicopter-euclidean', 'corner/FLU');
  await expect(page.getByTestId('family-piece-selection')).toHaveText('1 selected piece · 3 matching families');
  await pick(page, 'sphere', 'helicopter-spherical', 'corner/FRU');
  await expect(page.getByLabel('Twist family', { exact: true })).toHaveValue('UF');
  await expect(page.getByTestId('family-piece-selection')).toHaveText('2 selected pieces · 1 matching family');
  for (const host of await page.locator('.canvas-host').all()) {
    await expect(host).toHaveAttribute('data-selected-pieces', '["corner/FLU","corner/FRU"]');
    await expect(host).toHaveAttribute('data-destination-count', '0');
  }
  await page.screenshot({ path: 'test-results/helicopter-piece-family-selection.png', fullPage: true });
  await setViews(page, 'sphere-diagram');
  await pick(page, 'diagram', 'helicopter-port-diagram', 'corner/FRU');
  await expect(page.getByTestId('family-piece-selection')).toHaveText('1 selected piece · 3 matching families');
  const canvas = page.locator('[data-view-id="sphere"] canvas');
  await canvas.scrollIntoViewIfNeeded();
  const box = (await canvas.boundingBox())!;
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 60, box.y + box.height / 2 + 35, { steps: 8 });
  await page.mouse.up();
  await expect(page.getByTestId('family-piece-selection')).toHaveText('1 selected piece · 3 matching families');
  await page.locator('[data-view-id="diagram"] canvas').click({ position: { x: 5, y: 5 } });
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-count', '0');
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  expect(errors).toEqual([]);
});

test('incompatible selections hide angular controls and deselection restores them', async ({ page }) => {
  await open(page);
  const revision = await page.locator('.revision').textContent();
  for (const id of ['mechanism/UF', 'mechanism/UR'])
    await page.getByRole('button', { name: `Select ${id}`, exact: true }).click();
  await expect(page.getByTestId('family-piece-selection')).toHaveText('2 selected pieces · 0 matching families');
  await expect(page.getByLabel('Twist family', { exact: true })).toBeDisabled();
  await expect(page.locator('.angular-stop')).toHaveCount(0);
  await expect(page.getByText('No twist family includes all selected pieces', { exact: false })).toBeVisible();
  expect(await page.locator('.revision').textContent()).toBe(revision);
  await page.getByRole('button', { name: 'Remove mechanism/UR from selection', exact: true }).click();
  await expect(page.getByLabel('Twist family', { exact: true })).toHaveValue('UF');
  await expect(page.locator('.angular-stop')).toHaveCount(6);
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-count', '0');
});

test.describe('touch selection', () => {
  test.use({ hasTouch: true });
  test('toggles multiple pieces with ordinary taps', async ({ page }) => {
    await open(page);
    await setViews(page, 'cube-diagram');
    await pick(page, 'diagram', 'helicopter-port-diagram', 'corner/FLU', true);
    await pick(page, 'diagram', 'helicopter-port-diagram', 'corner/FRU', true);
    await expect(page.getByTestId('family-piece-selection')).toHaveText('2 selected pieces · 1 matching family');
    await expect(page.getByLabel('Twist family', { exact: true })).toHaveValue('UF');
    await page.getByRole('button', { name: 'Clear piece selection', exact: true }).tap();
    await expect(page.getByTestId('cube-view')).toHaveAttribute('data-selected-count', '0');
  });
});
