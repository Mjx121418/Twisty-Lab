import { test, expect, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { BufferAttribute, BufferGeometry, DoubleSide, Matrix4, Mesh, MeshBasicMaterial, OrthographicCamera, PerspectiveCamera, Raycaster, Vector2, Vector3 } from 'three';
import { createKernel, KernelSession, type MainModule, type SceneDescriptor } from '../../kernel/index';
import { pieceDestinations } from '../../web/src/destinations';

let runtime: MainModule;
test.beforeAll(async () => { runtime = await createKernel(); });
test.beforeEach(async ({ page }) => {
  await page.goto('./');
  await expect(page.getByTestId('state-digest')).toHaveText(/[0-9a-f]{64}/);
  await expect(page.locator('canvas')).toHaveCount(2);
});

async function resting(page: Page): Promise<void> {
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'false');
  const digest = await page.getByTestId('state-digest').textContent();
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-state-digest', digest!);
}

// Find a point on an actual destination surface using the public kernel's
// assets and frames, then click the canvas rather than a fallback move button.
async function clickDestination(page: Page, viewId: string, puzzle: string, realization: string, pieceId: string, operation: string, touch = false): Promise<void> {
  const host = page.getByTestId(viewId);
  await host.scrollIntoViewIfNeeded();
  const bounds = (await host.boundingBox())!;
  const session = new KernelSession(runtime, readFileSync(`packages/${puzzle}/${puzzle === 'helicopter' ? 'definition' : 'source'}.json`, 'utf8'));
  const geometry = runtime.createGeometry(session.native, readFileSync(`packages/${puzzle}/${realization}.json`, 'utf8'))!;
  const assets: BufferGeometry[] = [];
  const material = new MeshBasicMaterial();
  try {
    const scene = JSON.parse(geometry.sceneJSON()) as SceneDescriptor;
    const positions = new Float32Array(geometry.positions());
    const indices = new Uint32Array(geometry.indices());
    const assetMap = new Map(scene.meshAssets.map((asset) => {
      const mesh = new BufferGeometry(); assets.push(mesh);
      mesh.setAttribute('position', new BufferAttribute(positions.slice(asset.positionOffset, asset.positionOffset + asset.vertexCount * 3), 3));
      mesh.setIndex(new BufferAttribute(indices.slice(asset.indexOffset, asset.indexOffset + asset.indexCount), 1));
      return [asset.id, mesh];
    }));
    const camera = scene.diagram ? new OrthographicCamera() : new PerspectiveCamera(35, bounds.width / bounds.height, 0.1, 100);
    if (camera instanceof OrthographicCamera) {
      const horizontal = Math.max(6.5, 5.1 * bounds.width / bounds.height), vertical = horizontal * bounds.height / bounds.width;
      Object.assign(camera, { left: -horizontal, right: horizontal, top: vertical, bottom: -vertical, near: 0.1, far: 100 });
      camera.position.set(1.5, 0, 15); camera.lookAt(1.5, 0, 0);
    } else { camera.position.set(5, 4.2, 6); camera.lookAt(0, 0, 0); }
    camera.updateProjectionMatrix(); camera.updateMatrixWorld(true);
    material.side = DoubleSide;
    const ghosts: Mesh[] = [];
    for (const option of pieceDestinations(session, session.snapshot(), pieceId)) {
      geometry.prepareAnimationJSON(option.transitionRecord); geometry.sample(1);
      const transforms = new Float32Array(geometry.transforms());
      scene.visualParts.forEach((part, index) => {
        if (part.pieceId !== pieceId) return;
        const mesh = new Mesh(assetMap.get(part.meshAssetId)!, material);
        mesh.matrixAutoUpdate = false; mesh.matrix.fromArray(transforms, index * 16); mesh.updateMatrixWorld(true);
        mesh.userData.operation = option.request.operation;
        ghosts.push(mesh);
      });
    }
    const ray = new Raycaster();
    let point: Vector3 | undefined;
    for (const ghost of ghosts.filter((mesh) => mesh.userData.operation === operation)) {
      const vertices = ghost.geometry.getAttribute('position'), triangles = ghost.geometry.getIndex()!;
      for (let i = 0; i < triangles.count; i += 3) {
        const center = new Vector3();
        for (let j = 0; j < 3; j++) center.add(new Vector3().fromBufferAttribute(vertices, triangles.getX(i + j)));
        center.multiplyScalar(1 / 3).applyMatrix4(new Matrix4().copy(ghost.matrix)).project(camera);
        if (Math.abs(center.x) > 0.9 || Math.abs(center.y) > 0.8) continue;
        ray.setFromCamera(new Vector2(center.x, center.y), camera);
        const hits = ray.intersectObjects(ghosts);
        if (!hits.length || !hits.some((hit) => hit.distance - hits[0].distance < 0.035 && hit.object.userData.operation === operation)) continue;
        point = center; break;
      }
      if (point) break;
    }
    expect(point, `Visible destination for ${operation}`).toBeDefined();
    const x = (point!.x + 1) * bounds.width / 2, y = (1 - point!.y) * bounds.height / 2;
    if (touch) await page.touchscreen.tap(bounds.x + x, bounds.y + y);
    else await host.locator('canvas').click({ position: { x, y } });
    const chooser = host.getByRole('group', { name: 'Choose a twist for this destination', exact: true });
    if (await chooser.isVisible()) {
      const button = chooser.getByRole('button', { name: `Twist ${operation}`, exact: true });
      if (touch) await button.tap(); else await button.click();
    }
    await resting(page);
  } finally { for (const asset of assets) asset.dispose(); material.dispose(); geometry.delete(); session.dispose(); }
}

for (const [label, viewId, realization, pair] of [
  ['cube', 'cube-view', 'cube-euclidean', 'cube-sphere'],
  ['sphere', 'diagram-view', 'cube-spherical', 'cube-sphere'],
  ['diagram', 'diagram-view', 'cube-port-diagram', 'cube-diagram'],
]) test(`clicks a selected-piece destination in the ${label} and refreshes after undo/redo`, async ({ page }) => {
  await page.getByLabel('Geometry views').selectOption(pair);
  const initial = await page.getByTestId('state-digest').textContent();
  await page.getByRole('button', { name: 'Select corner/UFR', exact: true }).click();
  for (const id of ['cube-view', 'diagram-view']) {
    const host = page.getByTestId(id);
    await expect(host).toHaveAttribute('data-destination-piece', 'corner/UFR');
    await expect(host).toHaveAttribute('data-destination-count', '6');
    await expect(host).toHaveAttribute('data-destination-operations', "F,F',R,R',U,U'");
    // Only six copies of the selected corner's own visual parts are drawn.
    // Spherical corners use a body patch and a port patch on each face.
    const parts = Number(await host.getAttribute('data-destination-part-count'));
    expect(parts).toBe(id === 'cube-view' ? 24 : pair === 'cube-sphere' ? 36 : 18);
  }
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.locator('.revision')).toHaveText('rev. 0');
  const downloading = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Save session ↗' }).click();
  const saved = readFileSync((await (await downloading).path())!, 'utf8');
  await page.screenshot({ path: `test-results/destinations-${label}.png`, fullPage: true });
  await clickDestination(page, viewId, 'cube3', realization, 'corner/UFR', 'F');
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--algorithm', 'F', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await expect(page.getByTestId(viewId)).toHaveAttribute('data-selected-piece', 'corner/UFR');
  await expect(page.getByTestId(viewId)).toHaveAttribute('data-destination-operations', "D,D',F,F',R,R'");
  await page.getByRole('button', { name: '↶ Undo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(page.getByTestId(viewId)).toHaveAttribute('data-destination-operations', "F,F',R,R',U,U'");
  await page.getByRole('button', { name: '↷ Redo' }).click(); await resting(page);
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByLabel('Open session file').setInputFiles({ name: 'initial.json', mimeType: 'application/json', buffer: Buffer.from(saved) });
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await resting(page);
  await expect(page.getByTestId(viewId)).toHaveAttribute('data-destination-operations', "F,F',R,R',U,U'");
  await page.keyboard.press('Escape');
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-destination-count', '0');
});

test('offers a choice for a center destination shared by both twists', async ({ page }) => {
  await page.getByRole('button', { name: 'Select center/U', exact: true }).click();
  const host = page.getByTestId('diagram-view');
  await expect(host).toHaveAttribute('data-destination-count', '2');
  await expect(host).toHaveAttribute('data-destination-groups', '1');
  await clickDestination(page, 'diagram-view', 'cube3', 'cube-spherical', 'center/U', "U'");
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--algorithm', "U'", '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
});

test('filters bandaged destinations and keeps camera drags and keyboard focus pure', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('bandaged-uf-ufr');
  await page.getByRole('button', { name: 'Select bandage/UF-UFR', exact: true }).click();
  const initial = await page.getByTestId('state-digest').textContent();
  const host = page.getByTestId('cube-view');
  await expect(host).toHaveAttribute('data-destination-operations', "F,F',U,U'");
  await host.getByText('4 twists', { exact: true }).click();
  const option = host.getByRole('button', { name: 'Twist F', exact: true });
  await option.focus();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  const bounds = (await host.locator('canvas').boundingBox())!;
  await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
  await page.mouse.down();
  await page.mouse.move(bounds.x + bounds.width / 2 + 70, bounds.y + bounds.height / 2 + 20, { steps: 6 });
  await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, { steps: 6 });
  await page.mouse.up();
  await expect(page.getByTestId('state-digest')).toHaveText(initial!);
  await expect(host).toHaveAttribute('data-selected-piece', 'bandage/UF-UFR');
  await page.getByLabel('Geometry views').selectOption('sphere-diagram');
  await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-destination-operations', "F,F',U,U'");
  await page.getByTestId('diagram-view').getByText('4 twists', { exact: true }).click();
  await page.getByTestId('diagram-view').getByRole('button', { name: 'Twist F', exact: true }).click();
  await resting(page);
  await expect(page.getByTestId('state-digest')).not.toHaveText(initial!);
});

test('shows Helicopter candidates, executes a clicked destination, and clears previews during playback', async ({ page }) => {
  await page.getByLabel('Puzzle', { exact: true }).selectOption('helicopter');
  await page.getByLabel('Geometry views').selectOption('sphere-diagram');
  await page.getByRole('button', { name: 'Select corner/BDL', exact: true }).click();
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-destination-count', '15');
  await page.screenshot({ path: 'test-results/destinations-helicopter.png', fullPage: true });
  await clickDestination(page, 'cube-view', 'helicopter', 'helicopter-spherical', 'corner/BDL', 'BL_ab');
  const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--definition', 'packages/helicopter/definition.json', '--algorithm', 'BL_ab', '--json'], { encoding: 'utf8' }));
  await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
  await page.getByLabel('Animation speed').selectOption('450');
  await page.getByLabel('Algorithm', { exact: true }).fill('BL_ba UF_ab');
  await page.getByRole('button', { name: 'Play algorithm →' }).click();
  await expect(page.getByTestId('app')).toHaveAttribute('data-animating', 'true');
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-destination-count', '0');
  await resting(page);
  const session = new KernelSession(runtime, readFileSync('packages/helicopter/definition.json', 'utf8'));
  try {
    session.run('UF_ab', 'transactional');
    const expected = pieceDestinations(session, session.snapshot(), 'corner/BDL').map((option) => option.request.operation).join(',');
    await expect(page.getByTestId('cube-view')).toHaveAttribute('data-destination-operations', expected);
  } finally { session.dispose(); }
  await page.getByLabel('Puzzle', { exact: true }).selectOption('cube3');
  for (const id of ['cube-view', 'diagram-view']) await expect(page.getByTestId(id)).toHaveAttribute('data-destination-count', '0');
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test.describe('touch destinations', () => {
  test.use({ hasTouch: true, viewport: { width: 390, height: 844 } });
  test('taps a translucent piece on a small screen', async ({ page }) => {
    await page.getByRole('button', { name: 'Select corner/UFR', exact: true }).tap();
    await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-destination-count', '6');
    await clickDestination(page, 'diagram-view', 'cube3', 'cube-spherical', 'corner/UFR', 'F', true);
    const reference = JSON.parse(execFileSync('build/native/twisty', ['run', '--algorithm', 'F', '--json'], { encoding: 'utf8' }));
    await expect(page.getByTestId('state-digest')).toHaveText(reference.snapshot.stateDigest);
    await expect(page.getByTestId('diagram-view')).toHaveAttribute('data-selected-piece', 'corner/UFR');
  });
});
