import { readFileSync } from 'node:fs';
import * as THREE from 'three';
import { beforeAll, expect, it } from 'vitest';
import { createKernel, copyIntrinsicMeshes, GeometryInstance, KernelSession, type MainModule } from '../../kernel/index';
import { capUpperW, clippedMeshRotation, S3GpuProjection } from '../../web/src/s3Gpu';
import { s3Opacity } from '../../web/src/s3Fade';
let module: MainModule;
beforeAll(async () => { module = await createKernel(); });
const read = (file: string) => readFileSync(`packages/tesseract3/${file}`, 'utf8');

it('bounds curved triangles and only reuses clipped meshes under pole-fixing rotations', () => {
  const cosine = Math.cos(.3), center = new THREE.Vector4(.6, 0, 0, .8);
  const tangent = new THREE.Vector4(-.8, 0, 0, .6);
  for (let t = -.3; t <= .3; t += .01) {
    const p = center.clone().multiplyScalar(Math.cos(t)).addScaledVector(tangent, Math.sin(t));
    expect(p.w).toBeLessThanOrEqual(capUpperW(center.w, cosine) + 1e-12);
  }
  expect(capUpperW(.99, cosine)).toBe(1);
  const base = new THREE.Matrix4().makeRotationY(.4), current = new THREE.Matrix4().makeRotationX(.8).multiply(base);
  const out = new THREE.Matrix4();
  expect(clippedMeshRotation(current.elements, base.elements, out)).toBe(true);
  const expected = new THREE.Matrix4().makeRotationX(.8).elements;
  out.elements.forEach((value, i) => expect(value).toBeCloseTo(expected[i], 12));
  const tilted = current.clone(); tilted.elements[3] = .1;
  expect(clippedMeshRotation(tilted.elements, base.elements, out)).toBe(false);
});

it('exposes shared intrinsic meshes with owned storage after deleting producers', () => {
  const session = new KernelSession(module, read('package.json'));
  const definition = module.createGeometryDefinition(session.native, read('tesseract3-s3.json'))!;
  const native = definition.intrinsicMesh();
  const copied = copyIntrinsicMeshes(definition);
  const expected = Array.from(native.positions() as Float32Array);
  definition.delete(); session.dispose();
  try {
    expect(Array.from(native.positions() as Float32Array)).toEqual(expected);
    expect(Array.from(copied.positions)).toEqual(expected);
    expect(copied.descriptor.visualParts).toHaveLength(216);
    expect(copied.descriptor.meshAssets).toHaveLength(10);
    expect(copied.positions.length).toBe(4840 * 4);
  } finally { native.delete(); }
});

function setup() {
  const session = new KernelSession(module, read('package.json')); session.lightweightResults();
  const geometry = new GeometryInstance(module, session.native, read('tesseract3-s3.json'));
  const assets = new Map(geometry.scene.meshAssets.map(a => [a.id, new THREE.BufferGeometry()]));
  const meshes = geometry.scene.visualParts.map(p => {
    const mesh = new THREE.Mesh(assets.get(p.meshAssetId), new THREE.MeshStandardMaterial());
    mesh.matrixAutoUpdate = false; mesh.userData = { meshAssetId: p.meshAssetId }; return mesh;
  });
  const gpu = new S3GpuProjection(geometry, meshes, assets);
  return { session, geometry, gpu, meshes, dispose() {
    gpu.dispose(); geometry.dispose(); session.dispose();
    for (const mesh of meshes) mesh.material.dispose();
    for (const asset of assets.values()) asset.dispose();
  } };
}

it('installs committed states without projecting and refreshes exact drawing buffers on demand', () => {
  const {session,geometry,gpu,dispose} = setup();
  try {
    const positions=geometry.positions, normals=geometry.normals, revision=geometry.drawingRevision;
    const operations=['Xp_fY','Wp_fX','Ym_fX'];
    for (const operation of operations) {
      const record=session.move(operation).transitionRecords![0];
      geometry.prepare(record,false);geometry.evaluatePose(1);expect(gpu.update()).toBe(true);
      geometry.setState(session.stateText(),false);expect(gpu.update()).toBe(true);
      expect(geometry.positions).toBe(positions);expect(geometry.normals).toBe(normals);
      expect(geometry.drawingRevision).toBe(revision);expect(geometry.drawingIsCurrent).toBe(false);
      const frame=geometry.definition.frame(session.stateText());
      try {expect(Array.from(geometry.isometries)).toEqual(Array.from(frame.isometries() as Float64Array));}
      finally {frame.delete();}
      const endpoint=geometry.isometries.slice();geometry.evaluatePose(.2);
      expect(geometry.isometries).toEqual(endpoint); // State installation cancelled the old animation.
    }
    const frame=geometry.definition.frame(session.stateText());
    const expected=geometry.definition.projectWithOptions(frame,'stereographic',JSON.stringify(geometry.projectionOptions));
    try {
      geometry.refreshProjection();
      expect(geometry.drawingRevision).toBe(revision+1);expect(geometry.drawingIsCurrent).toBe(true);
      expect(geometry.positions).toEqual(new Float32Array(expected.positions() as Float32Array));
      expect(geometry.normals).toEqual(new Float32Array(expected.normals() as Float32Array));
      geometry.refreshProjection();expect(geometry.drawingRevision).toBe(revision+1);
    } finally {expected.delete();frame.delete();}
    // Rejected deferred state installs leave the current animation seekable.
    const record=session.move('Zp_fX').transitionRecords![0];
    geometry.prepare(record,false);geometry.evaluatePose(.25);
    const before=geometry.isometries.slice();
    expect(()=>geometry.setState('{}',false)).toThrow();
    expect(geometry.isometries).toEqual(before);
    geometry.evaluatePose(.75);expect(geometry.isometries).not.toEqual(before);
  } finally {dispose();}
});

it('runs all 184 turns in all eight focus charts without per-frame CPU projection', () => {
  const { session, geometry, gpu, dispose } = setup();
  try {
    // Scrambled colored placements must use the same prototype/pose contract.
    session.scramble(17, 12); geometry.setState(session.stateText());
    for (const group of (geometry.scene.controlLayout as { groups: { id: string; categories: { axes: { id: string }[] }[] }[] }).groups) {
      geometry.setProjection({ focusCell: group.id, guide: { group: group.id, axis: group.categories[0].axes[0].id } });
      gpu.captureDrawing();
      const revision = geometry.drawingRevision;
      for (const request of session.definition.operations) {
        const result = session.plan({ operation: request.id, parameters: {} });
        const record = result.transitionRecord ?? JSON.stringify(result.transition);
        geometry.prepare(record, false);
        for (const t of [0, .23, .5, .77, 1]) {
          geometry.evaluatePose(t);
          expect(gpu.update(), `${group.id}/${request.id}/${t}`).toBe(true);
        }
      }
      expect(geometry.drawingRevision).toBe(revision);
      expect(geometry.drawingIsCurrent).toBe(false);
    }
    // A smaller clipping ball cuts moving surfaces. Such frames must refresh
    // the portable clipped drawing rather than dividing across the pole.
    geometry.setProjection({ clipRadius: 1 }); gpu.captureDrawing();
    const result = session.plan({ operation: 'Xp_fY', parameters: {} });
    geometry.prepare(result.transitionRecord ?? JSON.stringify(result.transition), false);
    geometry.evaluatePose(.37);
    expect(gpu.update()).toBe(false);
    geometry.refreshProjection(); gpu.captureDrawing();
    expect(gpu.update()).toBe(true);
    expect(geometry.drawingIsCurrent).toBe(true);
    const c = Math.cos(.43), s = Math.sin(.43);
    geometry.setProjection({ viewIsometry: [c, 0, 0, -s, 0, 1, 0, 0, 0, 0, 1, 0, s, 0, 0, c], clipRadius: 2 });
    gpu.captureDrawing();
    const tilted = session.plan({ operation: 'Wp_fY', parameters: {} });
    geometry.prepare(tilted.transitionRecord ?? JSON.stringify(tilted.transition), false);
    geometry.evaluatePose(.37);
    expect(gpu.update()).toBe(false);
    geometry.refreshProjection(); gpu.captureDrawing();
    expect(gpu.update()).toBe(true);
  } finally { dispose(); }
}, 60_000);

it('matches portable projected positions and normals at intermediate poses', () => {
  const { session, geometry, gpu, meshes, dispose } = setup();
  try {
    const intrinsic = copyIntrinsicMeshes(geometry.definition);
    for (const focusCell of ['Wm', 'Xp', 'Zm']) {
      geometry.setProjection({ focusCell }); gpu.captureDrawing();
      for (const operation of ['Xp_fY', 'Wp_fX', 'Ym_vXYZ']) {
        // Choose one valid vertex turn without depending on its encoded axis name.
        const op = operation === 'Ym_vXYZ' ? session.definition.operations.find(o => o.family === 'Ym' && o.id.includes('_v'))!.id : operation;
        const planned = session.plan({ operation: op, parameters: {} });
        geometry.prepare(planned.transitionRecord ?? JSON.stringify(planned.transition), false);
        geometry.evaluatePose(.37); expect(gpu.update()).toBe(true);
        geometry.refreshProjection();
        for (let i = 0; i < meshes.length; i++) {
          const raw = meshes[i].geometry;
          const points = raw.getAttribute('intrinsicPosition'), normals = raw.getAttribute('intrinsicNormal');
          if (!points) continue; // Pole-clipped cached mesh.
          const view = new THREE.Matrix4().fromArray(geometry.scene.projection!.viewIsometry);
          const pose = new THREE.Matrix4().fromArray(geometry.isometries, i * 16);
          const combined = view.multiply(pose);
          const a = geometry.scene.meshAssets[i];
          const prototype = intrinsic.descriptor.meshAssets.find(a => a.id === intrinsic.descriptor.visualParts[i].meshAssetId)!;
          expect(a.vertexCount).toBe(prototype.vertexCount);
          for (let vertex = 0; vertex < a.vertexCount; vertex += 23) {
            const p = new THREE.Vector4().fromBufferAttribute(points as THREE.BufferAttribute, vertex).applyMatrix4(combined);
            const n = new THREE.Vector4().fromBufferAttribute(normals as THREE.BufferAttribute, vertex).applyMatrix4(combined);
            const normal = new THREE.Vector3(n.x, n.y, n.z).multiplyScalar(1 - p.w).addScaledVector(new THREE.Vector3(p.x, p.y, p.z), n.w).normalize();
            const point = new THREE.Vector3(p.x, p.y, p.z).divideScalar(1 - p.w);
            for (let j = 0; j < 3; j++) {
              expect(point.getComponent(j)).toBeCloseTo(geometry.positions[a.positionOffset + vertex * 3 + j], 4);
              expect(normal.getComponent(j)).toBeCloseTo(geometry.normals[a.positionOffset + vertex * 3 + j], 4);
            }
          }
        }
        gpu.captureDrawing();
      }
    }
  } finally { dispose(); }
}, 60_000);

it('rotates cached pole-clipped positions and normals exactly as CPU projection', () => {
  const { session, geometry, gpu, meshes, dispose } = setup();
  try {
    const before = { positions: geometry.positions.slice(), normals: geometry.normals.slice(), assets: structuredClone(geometry.scene.meshAssets) };
    const result = session.move('Wp_fX');
    geometry.prepare(result.transitionRecords![0], false); geometry.evaluatePose(.41);
    expect(gpu.update()).toBe(true);
    const cached = meshes.map((mesh, i) => mesh.geometry.getAttribute('intrinsicPosition') ? -1 : i).filter(i => i >= 0);
    expect(cached).toHaveLength(1);
    geometry.refreshProjection();
    for (const i of cached) {
      const a = before.assets[i], b = geometry.scene.meshAssets[i];
      expect(a.vertexCount).toBe(b.vertexCount);
      for (let v = 0; v < a.vertexCount; v += 11) {
        const p = new THREE.Vector3().fromArray(before.positions, a.positionOffset + 3 * v).applyMatrix4(meshes[i].matrix);
        const n = new THREE.Vector3().fromArray(before.normals, a.positionOffset + 3 * v).transformDirection(meshes[i].matrix);
        for (let j = 0; j < 3; j++) {
          expect(p.getComponent(j)).toBeCloseTo(geometry.positions[b.positionOffset + 3 * v + j], 4);
          expect(n.getComponent(j)).toBeCloseTo(geometry.normals[b.positionOffset + 3 * v + j], 4);
        }
      }
    }
  } finally { dispose(); }
});

it('shows all 26 surrounding outer pieces with a vertex fade, keeping the inner cell opaque and pole piece hidden', () => {
  const { session, geometry, gpu, meshes, dispose } = setup();
  try {
    session.scramble(29, 16); geometry.setState(session.stateText());
    for (const focusCell of ['Xp', 'Xm', 'Yp', 'Ym', 'Zp', 'Zm', 'Wp', 'Wm']) {
      geometry.setProjection({ focusCell }); gpu.captureDrawing(); expect(gpu.update()).toBe(true);
      const opaque = new Set<string>(), invisible = new Set<string>(), faded = new Set<string>();
      const view = new THREE.Matrix4().fromArray(geometry.scene.projection!.viewIsometry);
      const raw = copyIntrinsicMeshes(geometry.definition);
      const polePieces = new Set(geometry.scene.projection!.outerPieceIds);
      const visibleOpposite = new Set<string>();
      for (let i = 0; i < meshes.length; i++) {
        const part = geometry.scene.visualParts[i], depth = gpu.pieceDepths[part.pieceId], range = gpu.opacityRanges[i];
        if (range.min === 1) opaque.add(part.pieceId);
        else if (range.max === 0) invisible.add(part.pieceId);
        else faded.add(part.pieceId);
        if (polePieces.has(part.pieceId)) { expect(range).toEqual({ min: 0, max: 0 }); continue; }
        if (depth > .49) { visibleOpposite.add(part.pieceId); expect(range.max).toBeGreaterThan(0); }
        if (Math.abs(depth) < .49) continue;
        const matrix = new THREE.Matrix4().multiplyMatrices(view, new THREE.Matrix4().fromArray(geometry.isometries, i * 16));
        const prototype = raw.descriptor.meshAssets.find(a => a.id === raw.descriptor.visualParts[i].meshAssetId)!;
        let minimum = 1, maximum = 0;
        for (let v = 0; v < prototype.vertexCount; v++) {
          const w = new THREE.Vector4().fromArray(raw.positions, prototype.positionOffset + v * 4).applyMatrix4(matrix).w;
          const alpha = s3Opacity(w, gpu.fadeSettings.startRadius, gpu.fadeSettings.endRadius); minimum = Math.min(minimum, alpha); maximum = Math.max(maximum, alpha);
        }
        if (depth < 0) expect([minimum, maximum], `${focusCell}/${part.pieceId}`).toEqual([1, 1]);
        else { expect(minimum).toBeGreaterThan(0); expect(maximum).toBeLessThan(1); }
      }
      expect([opaque.size, faded.size, invisible.size]).toEqual([27, 52, 1]);
      expect(visibleOpposite.size).toBe(26);
    }
    let previous = 1;
    for (let i = 0; i <= 100; i++) {
      const opacity = s3Opacity(-1 / 3 + i / 75, gpu.fadeSettings.startRadius, gpu.fadeSettings.endRadius);
      expect(opacity).toBeLessThanOrEqual(previous); previous = opacity;
    }
    expect(s3Opacity(1, gpu.fadeSettings.startRadius, gpu.fadeSettings.endRadius)).toBe(0);
  } finally { dispose(); }
});
