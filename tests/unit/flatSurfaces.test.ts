import { readFileSync } from 'node:fs';
import Ajv from 'ajv';
import { expect, it } from 'vitest';
import { Matrix4, Vector3 } from 'three';
import { createKernel, KernelSession, copyFloatView, type SceneDescriptor } from '../../kernel/index';
import { cameraWindowPolygon, surfaceImageOrigin, surfaceImages, surfaceBounds, insideBounds, surfacePolygon, insidePolygon } from '../../web/src/flatSurface';

it.each(['torus','klein-bottle','hex-torus','hex-klein-bottle'])('keeps %s geometry intrinsic and reverses only reflected displayed images', async(name)=>{
  const hex=name.startsWith('hex-'),topology=hex?name.slice(4):name;
  const module=await createKernel(),folder=`packages/flat-${name}/`;
  const session=new KernelSession(module,readFileSync(folder+'package.json','utf8'));
  const realization=JSON.parse(readFileSync(folder+'flat-quotient.json','utf8'));
  const ajv=new Ajv({strict:false});
  expect(ajv.compile(JSON.parse(readFileSync('schemas/realization.schema.json','utf8')))(realization)).toBe(true);
  const geometry=module.createGeometry(session.native,JSON.stringify(realization))!;
  try{
    const scene=JSON.parse(geometry.sceneJSON()) as SceneDescriptor,surface=scene.flatSurface!;
    expect(scene.visualParts).toHaveLength(hex?100:116);expect(surface.topology).toBe(topology);
    expect(scene.requiredCapabilities).toContain('quotient-surface');
    const square=surfaceImages(surface,'square'),tiles=surfaceImages(surface,'tiles');
    expect(square).toHaveLength(9);expect(tiles).toHaveLength(25);
    expect(surfaceBounds(surface,'square')).toEqual(hex?[0,0,3,Math.sqrt(3)]:[0,0,2,2]);
    expect(surfaceBounds(surface,'tiles')).toEqual(hex?[-3,-Math.sqrt(3),6,2*Math.sqrt(3)]:[-2,-2,4,4]);
    const polygon=surfacePolygon(surface,'square');
    const center:[number,number]=hex?[1.5,Math.sqrt(3)/2]:[1,1];
    for(const zoom of [.7,2,5]){
      const target:[number,number]=[45.3,-29.6],window=cameraWindowPolygon(polygon,center,target,zoom);
      window.forEach((p,i)=>p.forEach((v,axis)=>expect((v-target[axis])*zoom).toBeCloseTo(polygon[i][axis]-center[axis],10)));
      expect(insidePolygon(window,...target)).toBe(true);
      expect(insidePolygon(window,100,100)).toBe(false);
    }
    const basis=surface.latticeBasis??[[2,0],[0,2]],u=37.3,v=-11.6;
    expect(surfaceImageOrigin(surface,u*basis[0][0]+v*basis[1][0],u*basis[0][1]+v*basis[1][1])).toEqual([37,-12]);
    const recycled=surfaceImages(surface,'tiles',[37,-12],3);
    expect(recycled).toHaveLength(49);
    expect(recycled[0].x).toBe(34);expect(recycled[0].y).toBe(-15);
    expect(recycled.some(i=>i.reflected)).toBe(topology==='klein-bottle');
    const planePool=surfaceImages(surface,'tiles',[37,-12],hex?[6,4]:4);
    expect(planePool).toHaveLength(hex?117:81);
    expect(insidePolygon(polygon,1.5,Math.sqrt(3)/2)).toBe(true);
    if(hex){
      expect(insidePolygon(polygon,.1,1.5)).toBe(false);
      const expected={A:[0,0],B:[1,0],C:[.5,Math.sqrt(3)/2],D:[1.5,Math.sqrt(3)/2]};
      for(const [name,center] of Object.entries(expected))center.forEach((v,i)=>expect(surface.diskCenters![name][i]).toBeCloseTo(v,12));
    }
    expect(insideBounds([0,0,2,2],-.1,1)).toBe(false);
    expect(tiles.some(i=>i.reflected)).toBe(topology==='klein-bottle');
    const [a,b]=surface.deckGenerators.map(m=>new Matrix4().fromArray(m));
    const conjugate=b.clone().multiply(a).multiply(b.clone().invert());
    const expected=topology==='torus'?a:a.clone().invert();
    conjugate.elements.forEach((v,i)=>expect(v).toBeCloseTo(expected.elements[i],9));
    if(hex&&topology==='klein-bottle'){
      // Checkerboard display addresses must describe actual deck elements,
      // not arbitrary reflected copies that happen to cover the plane.
      const power=(m:Matrix4,n:number)=>{const out=new Matrix4(),factor=n<0?m.clone().invert():m;for(let i=0;i<Math.abs(n);i++)out.multiply(factor);return out;};
      for(const image of tiles){
        const n=image.x+image.y,parity=((n%2)+2)%2,m=(image.x-image.y-parity)/2;
        const deck=power(a,m).multiply(power(b,n));
        deck.elements.forEach((v,i)=>expect(v).toBeCloseTo(image.matrix.elements[i],9));
      }
      const bottom=[new Vector3(0,0,0),new Vector3(2,0,0)].map(p=>p.applyMatrix4(b));
      expect(bottom[0].distanceTo(new Vector3(2,0,0))).toBeLessThan(1e-9);
      expect(bottom[1].distanceTo(new Vector3(3,Math.sqrt(3),0))).toBeLessThan(1e-9);
    }
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
    expect(session.run(hex?'(A)6 B2 B2 B2 C3 C3':"(A)4 B2 B2",'transactional').status).toBe('Committed');
    expect(session.snapshot().stateDigest).toBe(origin);
    expect(session.run("A B C D (A B C D)'",'transactional').status).toBe('Committed');
    expect(session.snapshot().stateDigest).toBe(origin);
    const saved=session.save({viewPair:'square-tiles'}),restored=new KernelSession(module,session);
    try{expect(restored.load(saved).status).toBe('Loaded');expect(restored.snapshot().stateDigest).toBe(origin);}
    finally{restored.dispose();}
  }finally{geometry.delete();session.dispose();}
});
