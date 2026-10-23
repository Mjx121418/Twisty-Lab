import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { expect, it } from 'vitest';
import { Matrix4, Vector3 } from 'three';
import { createKernel, KernelSession, copyFloatView, copyIndexView, type SceneDescriptor } from '../../kernel/index';
import { surfaceImageOrigin, surfaceImages, surfacePolygon } from '../../web/src/flatSurface';

for(const six of [false,true])it(`333 ${six?6:3} disks: distinct ${six?'shaded':'nearest-center'} stickers and complete quotient coverage during turns`,async()=>{
  const folder=`packages/flat-333${six?'-6':''}/`;
  const source=readFileSync(folder+'package.json','utf8');
  const realization=JSON.parse(readFileSync(folder+'flat-quotient.json','utf8'));
  const module=await createKernel(),session=new KernelSession(module,source);
  const geometry=module.createGeometry(session.native,JSON.stringify(realization))!;
  try{
    const scene=JSON.parse(geometry.sceneJSON()) as SceneDescriptor,surface=scene.flatSurface!;
    expect(surface.topology).toBe('orbifold-333');
    expect(surface.deckGenerators).toHaveLength(3);expect(surface.conePoints).toHaveLength(3);
    expect(scene.visualParts).toHaveLength(six?122:23);
    expect(scene.visualParts.some(part=>part.materialBindingId==='ink')).toBe(false);
    expect(surfacePolygon(surface,'square')).toEqual([[0,0],[1,0],[1.5,Math.sqrt(3)/2],[.5,Math.sqrt(3)/2]]);
    const images=surfaceImages(surface,'tiles',[0,0],1);
    expect(images).toHaveLength(27);
    const keys=new Set<string>();
    for(const {matrix,reflected} of images){
      expect(reflected).toBe(false);
      const e=matrix.elements,u=e[12]-e[13]/Math.sqrt(3),v=2*e[13]/Math.sqrt(3);
      expect((u-v)/3).toBeCloseTo(Math.round((u-v)/3),9);
      expect((u+2*v)/3).toBeCloseTo(Math.round((u+2*v)/3),9);
      keys.add(e.map(x=>Math.round(x*1e8)).join(','));
    }
    expect(keys.size).toBe(images.length);
    const [a,b]=surface.deckGenerators;
    expect(surfaceImageOrigin(surface,7.2*a[12]-3.4*b[12],7.2*a[13]-3.4*b[13])).toEqual([7,-4]);
    const origin=session.snapshot().stateDigest;
    expect(session.run(six?'A A B B C C (D)6 (E)6 (F)6':'A A B B C C','transactional').status).toBe('Committed');
    expect(session.snapshot().stateDigest).toBe(origin);
    const word=six?"A D E2 F3 B C D' F2'":"A B C A' C B";
    const native=JSON.parse(execFileSync('build/native/twisty',['run','--definition',folder+'package.json','--algorithm',word,'--policy','transactional','--json'],{encoding:'utf8'}));
    const fresh=new KernelSession(module,source);
    try{expect(fresh.run(word,'transactional').snapshot).toEqual(native.snapshot);}finally{fresh.dispose();}
    const positions=copyFloatView(geometry.positions()),indices=copyIndexView(geometry.indices());
    const assets=new Map(scene.meshAssets.map(a=>[a.id,a]));
    const bodies=scene.visualParts.flatMap((part,index)=>part.meshAssetId.endsWith('/body')?[{part,index}]:[]);
    expect(bodies).toHaveLength(six?26:8);
    const bodyIndices=new Map(bodies.map(({part,index})=>[part.pieceId,index]));
    const stickers=scene.visualParts.flatMap((part,index)=>part.role==='port'?[{part,index,asset:assets.get(part.meshAssetId)!}]:[]);
    const centers=Object.entries(surface.diskCenters!).flatMap(([color,p])=>images.map(image=>({
      color,point:new Vector3(...p,0).applyMatrix4(image.matrix),
    })));
    const home=copyFloatView(geometry.transforms()),catalog=JSON.parse(source).catalog;
    const colorAreas=new Map<string,Map<string,{area:number;moment:Vector3}>>();
    const shadeAt=(p:Vector3):string=>{
      const u=p.x-p.y/Math.sqrt(3),v=2*p.y/Math.sqrt(3);
      return u-Math.floor(u)+v-Math.floor(v)<1?'light':'dark';
    };
    const deck=[new Matrix4(),...surface.deckGenerators.map(m=>new Matrix4().fromArray(m))];
    const meshArea=(asset:typeof scene.meshAssets[number]):number=>{
      let area=0;
      for(let k=asset.indexOffset;k<asset.indexOffset+asset.indexCount;k+=3){
        const [a,b,c]=[0,1,2].map(i=>new Vector3().fromArray(positions,asset.positionOffset+indices[k+i]*3));
        area+=new Vector3().crossVectors(b.sub(a),c.sub(a)).length()/2;
      }
      return area;
    };
    for(const {part,index,asset} of stickers){
      expect(part.materialBindingId).toBe(catalog.pieces[part.pieceId].portLabels[part.portId!]);
      expect(part.materialBindingId).toMatch(six?/^[A-F]-(light|dark)$/:/^[ABC]$/);
      const [hue,shade]=part.materialBindingId.split('-');
      const frame=new Matrix4().fromArray(home,index*16);
      for(let vertex=0;vertex<asset.vertexCount;vertex++){
        const p=new Vector3().fromArray(positions,asset.positionOffset+vertex*3).applyMatrix4(frame);
        const nearest=Math.min(...centers.map(c=>p.distanceToSquared(c.point)));
        const assigned=Math.min(...centers.filter(c=>c.color===hue).map(c=>p.distanceToSquared(c.point)));
        expect(assigned,`${part.visualPartId} vertex ${vertex} must use its nearest disk color`).toBeLessThanOrEqual(nearest+1e-6);
      }
      const colors=colorAreas.get(part.pieceId)??new Map<string,{area:number;moment:Vector3}>();
      const entry=colors.get(part.materialBindingId)??{area:0,moment:new Vector3()};
      for(let k=asset.indexOffset;k<asset.indexOffset+asset.indexCount;k+=3){
        const [a,b,c]=[0,1,2].map(i=>new Vector3().fromArray(positions,asset.positionOffset+indices[k+i]*3));
        const area=new Vector3().crossVectors(b.clone().sub(a),c.clone().sub(a)).length()/2;
        const center=a.add(b).add(c).multiplyScalar(1/3);
        entry.area+=area;entry.moment.addScaledVector(center,area);
        const world=center.clone().applyMatrix4(frame);
        if(six)for(const image of deck)expect(shadeAt(world.clone().applyMatrix4(image)),part.visualPartId).toBe(shade);
      }
      colors.set(part.materialBindingId,entry);colorAreas.set(part.pieceId,colors);
    }
    for(const family of ['edge','vertex']){
      const pieces=Object.keys(catalog.pieces).filter(id=>catalog.pieces[id].domain===family);
      const signatures=pieces.map(id=>{
        if(six)return [...colorAreas.get(id)!.keys()].sort().join(',');
        // The plain triple pieces share three hues, in opposite cyclic
        // orders. Reflection is not an allowed orientation on this orbifold.
        const labels=Object.values(catalog.pieces[id].portLabels) as string[];
        return labels.map((_,i)=>[...labels.slice(i),...labels.slice(0,i)].join(',')).sort()[0];
      });
      expect(new Set(signatures).size).toBe(family==='edge'?(six?12:3):(six?8:2));
      for(const id of pieces){
        // A nonzero color-area centroid cannot be fixed by a nonidentity
        // rotation, proving visibility of every edge flip and triple twist.
        expect([...colorAreas.get(id)!.values()].some(c=>c.moment.length()/c.area>1e-4)).toBe(true);
      }
    }
    for(const {part} of bodies){
      const colors=colorAreas.get(part.pieceId)!;
      expect([...colors.values()].reduce((sum,c)=>sum+c.area,0)/meshArea(assets.get(part.meshAssetId)!)).toBeCloseTo(.94**2,6);
      if(catalog.pieces[part.pieceId].domain==='face'){
        // The full ordinary center has a light and a dark half. Its color
        // centroid detects all six rotations without an orientation mark.
        expect(colors.size).toBe(2);
        expect([...colors.values()].every(c=>c.moment.length()/c.area>1e-4)).toBe(true);
      }
      if(catalog.pieces[part.pieceId].domain==='cone')expect(colors.size).toBe(six?2:1);
    }
    const insideTriangle=(p:Vector3,a:number,b:number,c:number,offset:number):boolean=>{
      const cross=(u:number,v:number)=>(positions[offset+v*3]-positions[offset+u*3])*(p.y-positions[offset+u*3+1])-
        (positions[offset+v*3+1]-positions[offset+u*3+1])*(p.x-positions[offset+u*3]);
      const sides=[cross(a,b),cross(b,c),cross(c,a)];
      return sides.every(v=>v>=-1e-9)||sides.every(v=>v<=1e-9);
    };
    for(const operation of six?['A','A2','D','E3','F2\'']:['A','A2','C']){
      geometry.setStateJSON(session.stateText());
      const planned=session.plan({operation,parameters:{}});
      expect(JSON.parse(geometry.prepareAnimationJSON(planned.transitionRecord!)).status).toBe('Prepared');
      for(const progress of [0,.37,.999,1]){
        geometry.sample(progress);
        const frames=copyFloatView(geometry.transforms());
        for(const {part,index} of stickers){
          const body=bodyIndices.get(part.pieceId)!;
          expect(frames.slice(index*16,index*16+16)).toEqual(frames.slice(body*16,body*16+16));
        }
        const instances=bodies.flatMap(({part,index})=>images.map(image=>({
          inverse:new Matrix4().multiplyMatrices(image.matrix,new Matrix4().fromArray(frames,index*16)).invert(),asset:assets.get(part.meshAssetId)!,
        })));
        let checked=0;
        for(let i=0;i<12;i++)for(let j=0;j<12;j++){
          const u=(i+.213)/12,v=(j+.381)/12,p=new Vector3(u+v/2,v*Math.sqrt(3)/2,0);
          const center=surface.diskCenters![operation[0]],radius=surface.cutRadius!;
          if(images.some(image=>Math.abs(p.distanceTo(new Vector3(...center,0).applyMatrix4(image.matrix))-radius)<.003))continue;
          let count=0;
          for(const {inverse,asset} of instances){
            const local=p.clone().applyMatrix4(inverse);
            for(let k=asset.indexOffset;k<asset.indexOffset+asset.indexCount;k+=3)
              if(insideTriangle(local,indices[k],indices[k+1],indices[k+2],asset.positionOffset)){count++;break;}
          }
          expect(count,`${operation} t=${progress}: coverage at ${u},${v}`).toBe(1);
          checked++;
        }
        expect(checked).toBeGreaterThan(110);
      }
    }
    const restored=new KernelSession(module,source);
    try{expect(restored.load(session.save({views:['fundamental-domain','tiled-plane']})).status).toBe('Loaded');expect(restored.snapshot().stateDigest).toBe(origin);}
    finally{restored.dispose();}
  }finally{geometry.delete();session.dispose();}
});

it('keeps the three-disk 333 model at 96 marked states',()=>{
  const {source}=JSON.parse(readFileSync('packages/flat-333/package.json','utf8'));
  const origin=source.domains.flatMap((d:any)=>d.pieces.map((p:any)=>p.initial)) as string[];
  const moves=['A','B','C'].map(name=>source.domains.flatMap((d:any)=>d.pieces.map(()=>d.moves.find((m:any)=>m.id===name).transport)));
  const queue=[origin],seen=new Set([JSON.stringify(origin)]);
  for(let i=0;i<queue.length;i++)for(const move of moves){
    const target=queue[i].map((p,j)=>move[j][p]??p),key=JSON.stringify(target);
    if(!seen.has(key)){seen.add(key);queue.push(target);}
    expect(seen.size).toBeLessThanOrEqual(96);
  }
  expect(seen.size).toBe(96);
});
