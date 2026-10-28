import { readFileSync } from 'node:fs';
import * as THREE from 'three';
import { expect, it } from 'vitest';
import { createKernel, GeometryInstance, KernelSession, type CellAxisLayout } from '../../kernel/index';
import { S3GpuProjection } from '../../web/src/s3Gpu';

it('loads the large authoring source through the independent portable language API', async () => {
  const module = await createKernel();
  const text = readFileSync('packages/cell24/puzzle.pzl.json','utf8');
  expect(Buffer.byteLength(text)).toBeGreaterThan(8*1024*1024);
  const source = JSON.parse(text);
  let offset = 0;
  const initial = source.domains.flatMap((d: {placements:string[];pieces:{initial:string}[]}) => {
    const values = d.pieces.map(p => offset+d.placements.indexOf(p.initial));
    offset += d.placements.length;return values;
  });
  const puzzle = new module.Puzzle(text);
  try {
    const selected = JSON.parse(puzzle.queryJSON(JSON.stringify({op:'bind',state:initial,request:'Xp_fXYZ'})));
    expect(selected.status).toBe('Selected');
    const plan = JSON.parse(puzzle.queryJSON(JSON.stringify({op:'plan',state:initial,request:'Xp_fXYZ',choices:selected.choices})));
    expect(plan.status).toBe('Permitted');expect(plan.transition.actions).toHaveLength(105);
  } finally {puzzle.delete();}
});

it('24-cell GPU transforms preserve complete cut membership in every chart and ordinary turn', async () => {
  const module = await createKernel();
  const session = new KernelSession(module, readFileSync('packages/cell24/package.json','utf8'));
  session.lightweightResults();
  const geometry = new GeometryInstance(module,session.native,readFileSync('packages/cell24/cell24-s3.json','utf8'));
  const assets = new Map(geometry.scene.meshAssets.map(a=>[a.id,new THREE.BufferGeometry()]));
  const meshes = geometry.scene.visualParts.map(p=>{
    const mesh=new THREE.Mesh(assets.get(p.meshAssetId),new THREE.MeshStandardMaterial());
    mesh.userData={meshAssetId:p.meshAssetId};return mesh;
  });
  const gpu=new S3GpuProjection(geometry,meshes,assets,2/3);
  try {
    const layout=geometry.scene.controlLayout as CellAxisLayout;
    for (const group of layout.groups) {
      geometry.setProjection({focusCell:group.id,guide:{group:group.id,axis:group.categories[0].axes[0].id}});
      gpu.captureDrawing();expect(gpu.update()).toBe(true);
      expect(gpu.guidePieces.size).toBe(105);
      const neighbors = layout.groups.filter(g => g.center.reduce((dot,v,j) => dot + v * group.center[j],0) >= .5 - 1e-8);
      expect(neighbors).toHaveLength(9);
      const participants = (g: typeof group) => session.plan({operation:g.categories[0].axes[0].turns[0].operation,parameters:{}}).transition!.pieceActions.map(a=>a.pieceId);
      const focused = new Set(participants(group));
      const allowed = new Set(neighbors.flatMap(participants));
      const revision = geometry.drawingRevision;
      for (const radius of [gpu.fadeRange.minRadius, .8, gpu.fadeRange.maxStartRadius]) {
        gpu.setFadeStartRadius(radius);expect(gpu.update()).toBe(true);
        const opaque = new Set<string>(), hidden = new Set<string>();
        for (let i=0;i<geometry.scene.visualParts.length;i++) {
          const id=geometry.scene.visualParts[i].pieceId, opacity=gpu.opacityRanges[i];
          if (opacity.min===1) opaque.add(id);
          if (opacity.max===0) hidden.add(id);
          if (focused.has(id)) expect(opacity,`${group.id}/${id}/${radius}`).toEqual({min:1,max:1});
          if (!allowed.has(id)) expect(opacity,`${group.id}/${id}/${radius}`).toEqual({min:0,max:0});
          else expect(opacity.max,`${group.id}/${id}/${radius}`).toBeGreaterThan(0);
        }
        expect(hidden.size).toBe(672-allowed.size);
        if (radius===gpu.fadeRange.minRadius) expect(opaque.size).toBe(105);
      }
      const visible = gpu.opacityRanges.filter(o=>o.max>0).length;
      gpu.setFadeEndRadius(gpu.fadeRange.minRadius+.01);expect(gpu.update()).toBe(true);
      expect(gpu.fadeSettings.startRadius).toBe(gpu.fadeRange.minRadius);
      expect(gpu.fadeSettings.maxStartRadius).toBe(gpu.fadeRange.minRadius);
      expect(gpu.opacityRanges.filter(o=>o.max>0).length).toBeLessThan(visible);
      for (let i=0;i<geometry.scene.visualParts.length;i++) if (focused.has(geometry.scene.visualParts[i].pieceId))
        expect(gpu.opacityRanges[i]).toEqual({min:1,max:1});
      gpu.setFadeEndRadius(gpu.fadeRange.endRadius);
      expect(geometry.drawingRevision).toBe(revision);
      expect(gpu.pieceDepths[`center-${group.id}/0`]).toBeCloseTo(-1,10);
    }
    gpu.setFadeStartRadius(gpu.fadeRange.minRadius);
    geometry.setProjection({focusCell:'Wm'});gpu.captureDrawing();
    const revision=geometry.drawingRevision;
    for (const op of session.definition.operations) {
      const result=session.plan({operation:op.id,parameters:{}});
      expect(result.transition?.pieceActions).toHaveLength(105);
      geometry.prepare(result.transitionRecord??JSON.stringify(result.transition),false);
      for (const t of [.23,.5,.77]) {
        geometry.evaluatePose(t);
        expect(gpu.update(),`${op.id}/${t}`).toBe(true);
      }
    }
    expect(geometry.drawingRevision).toBe(revision);
    // The 24 cuts are explicit metadata: changing a prototype's placement must
    // still produce the same pole and guide membership as the portable adapter.
    expect(session.run("Xp_fXYZ Hpppp_eXY Yp_vX Hpmmp_vZ", "transactional").status).toBe("Committed");geometry.setState(session.stateText());
    geometry.setProjection({focusCell:'Hpppp',guide:{group:'Yp',axis:layout.groups[2].categories[0].axes[0].id}});
    gpu.captureDrawing();expect(gpu.update()).toBe(true);
    expect([...gpu.guidePieces].sort()).toEqual([...geometry.scene.guidePieces!].sort());
    const focus=layout.groups.find(g=>g.id==='Hpppp')!;
    const allowed=new Set(layout.groups.filter(g=>g.center.reduce((dot,v,j)=>dot+v*focus.center[j],0)>=.5-1e-8)
      .flatMap(g=>session.plan({operation:g.categories[0].axes[0].turns[0].operation,parameters:{}}).transition!.pieceActions.map(a=>a.pieceId)));
    for (let i=0;i<geometry.scene.visualParts.length;i++) if (!allowed.has(geometry.scene.visualParts[i].pieceId))
      expect(gpu.opacityRanges[i]).toEqual({min:0,max:0});
    for (const [id,depth] of Object.entries(geometry.scene.projection!.pieceDepths))
      expect(gpu.pieceDepths[id]).toBeCloseTo(depth,10);
  } finally {
    gpu.dispose();geometry.dispose();session.dispose();
    for (const mesh of meshes) mesh.material.dispose();
    for (const asset of assets.values())asset.dispose();
  }
},120_000);
