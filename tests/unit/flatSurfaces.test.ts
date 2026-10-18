import { readFileSync } from 'node:fs';
import Ajv from 'ajv';
import { expect, it } from 'vitest';
import { Matrix4, Vector3 } from 'three';
import { createKernel, KernelSession, copyFloatView, type SceneDescriptor } from '../../kernel/index';
import { surfaceImages, surfaceBounds, insideBounds } from '../../web/src/flatSurface';

it.each(['torus','klein-bottle'])('keeps %s geometry intrinsic and reverses only reflected displayed images', async(topology)=>{
  const module=await createKernel(),folder=`packages/flat-${topology}/`;
  const session=new KernelSession(module,readFileSync(folder+'relational-package.json','utf8'));
  const realization=JSON.parse(readFileSync(folder+'flat-quotient.json','utf8'));
  const ajv=new Ajv({strict:false});
  expect(ajv.compile(JSON.parse(readFileSync('schemas/realization.schema.json','utf8')))(realization)).toBe(true);
  const geometry=module.createGeometry(session.native,JSON.stringify(realization))!;
  try{
    const scene=JSON.parse(geometry.sceneJSON()) as SceneDescriptor,surface=scene.flatSurface!;
    expect(scene.visualParts).toHaveLength(116);expect(surface.topology).toBe(topology);
    expect(scene.requiredCapabilities).toContain('quotient-surface');
    const square=surfaceImages(surface,'square'),tiles=surfaceImages(surface,'tiles');
    expect(square).toHaveLength(9);expect(tiles).toHaveLength(25);
    expect(surfaceBounds(surface,'square')).toEqual([0,0,2,2]);expect(surfaceBounds(surface,'tiles')).toEqual([-2,-2,4,4]);
    expect(insideBounds([0,0,2,2],-.1,1)).toBe(false);
    expect(tiles.some(i=>i.reflected)).toBe(topology==='klein-bottle');
    const [a,b]=surface.deckGenerators.map(m=>new Matrix4().fromArray(m));
    const conjugate=b.clone().multiply(a).multiply(b.clone().invert());
    const expected=topology==='torus'?a:a.clone().invert();
    conjugate.elements.forEach((v,i)=>expect(v).toBeCloseTo(expected.elements[i],9));
    const result=session.plan({operation:'A',parameters:{}});expect(result.status).toBe('Legal');
    expect(JSON.parse(geometry.prepareAnimationJSON(result.transitionRecord!)).status).toBe('Prepared');
    const source=copyFloatView(geometry.transforms() as Float32Array);
    geometry.sample(.5);const middle=copyFloatView(geometry.transforms() as Float32Array);
    const part=scene.visualParts.findIndex(p=>p.pieceId==='face-00');
    for(const image of square){
      const start=new Matrix4().multiplyMatrices(image.matrix,new Matrix4().fromArray(source,part*16));
      const mid=new Matrix4().multiplyMatrices(image.matrix,new Matrix4().fromArray(middle,part*16));
      const o=new Vector3(0,0,0).applyMatrix4(start),p=new Vector3(.1,0,0).applyMatrix4(start).sub(o),
        q=new Vector3(.1,0,0).applyMatrix4(mid).sub(o);
      expect(Math.sign(p.x*q.y-p.y*q.x)).toBe(image.reflected?1:-1);
    }
    const origin=session.snapshot().stateDigest;
    expect(session.run("A B C D (A B C D)'",'transactional').status).toBe('Committed');
    expect(session.snapshot().stateDigest).toBe(origin);
    const saved=session.save({viewPair:'square-tiles'}),restored=new KernelSession(module,session);
    try{expect(restored.load(saved).status).toBe('Loaded');expect(restored.snapshot().stateDigest).toBe(origin);}
    finally{restored.dispose();}
  }finally{geometry.delete();session.dispose();}
});
