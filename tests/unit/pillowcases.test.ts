import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { expect, it } from 'vitest';
import { Matrix4, Vector3 } from 'three';
import { createKernel, KernelSession, copyFloatView, copyIndexView, type SceneDescriptor } from '../../kernel/index';
import { insidePolygon, surfaceImageOrigin, surfaceImages, surfacePolygon } from '../../web/src/flatSurface';

for(const six of [false,true])it(`realizes the ${six?6:4}-disk pillowcase without overlapping cone copies`,async()=>{
  const folder=`packages/flat-pillowcase${six?'-6':''}/`;
  const source=readFileSync(folder+'package.json','utf8');
  const realization=JSON.parse(readFileSync(folder+'flat-quotient.json','utf8'));
  const module=await createKernel(),session=new KernelSession(module,source);
  const geometry=module.createGeometry(session.native,JSON.stringify(realization))!;
  try{
    const scene=JSON.parse(geometry.sceneJSON()) as SceneDescriptor,surface=scene.flatSurface!;
    expect(surface.topology).toBe('pillowcase');expect(surface.deckGenerators).toHaveLength(3);
    expect(surface.conePoints).toHaveLength(4);expect(scene.visualParts).toHaveLength(six?122:64);
    expect(scene.requiredCapabilities).toContain('quotient-orbifold');
    const polygon=surfacePolygon(surface,'square');
    expect(polygon).toEqual([[1,0],[2,1],[1,2],[0,1]]);
    expect(insidePolygon(polygon,.1,.1)).toBe(false);
    expect(surfaceImageOrigin(surface,1+7.3-(-5.2),7.3+(-5.2))).toEqual([7,-6]);
    const images=surfaceImages(surface,'tiles',[0,0],2);
    for(const {matrix,x,y,reflected} of images){
      expect(reflected).toBe(false);
      const odd=(x+y)%2!==0,e=matrix.elements;
      expect(e[0]).toBe(odd?-1:1);expect(e[5]).toBe(odd?-1:1);
      // Every displayed map belongs to translations (2Z)^2 or their coset I.
      expect((e[12]-(odd?1:0))/2).toBeCloseTo(Math.round((e[12]-(odd?1:0))/2),10);
      expect((e[13]-(odd?1:0))/2).toBeCloseTo(Math.round((e[13]-(odd?1:0))/2),10);
    }
    const origin=session.snapshot().stateDigest;
    expect(session.run(six?'A A B B C C D D (E)4 (F)4':'A A B B C C D D','transactional').status).toBe('Committed');
    expect(session.snapshot().stateDigest).toBe(origin);
    const word=six?"A E F B C E2 F' D":"A B C D A' C";
    const native=JSON.parse(execFileSync('build/native/twisty',['run','--definition',folder+'package.json','--algorithm',word,'--policy','transactional','--json'],{encoding:'utf8'}));
    const fresh=new KernelSession(module,source);
    try{expect(fresh.run(word,'transactional').snapshot).toEqual(native.snapshot);}finally{fresh.dispose();}
    // A complete covering-plane partition at intermediate angles catches
    // double-sized cone assets, missing copies, and incorrect canonical lifts.
    const positions=copyFloatView(geometry.positions()),indices=copyIndexView(geometry.indices());
    const bodies=scene.visualParts.map((part,index)=>({part,index})).filter(({part})=>part.meshAssetId.endsWith('/body'));
    expect(bodies).toHaveLength(six?34:18);
    const assets=new Map(scene.meshAssets.map(asset=>[asset.id,asset]));
    const insideTriangle=(p:Vector3,a:number,b:number,c:number,offset:number):boolean=>{
      const cross=(u:number,v:number)=>(positions[offset+v*3]-positions[offset+u*3])*(p.y-positions[offset+u*3+1])-
        (positions[offset+v*3+1]-positions[offset+u*3+1])*(p.x-positions[offset+u*3]);
      const sides=[cross(a,b),cross(b,c),cross(c,a)];
      return sides.every(v=>v>=-1e-9)||sides.every(v=>v<=1e-9);
    };
    // Solved colors are a Voronoi decoration of the whole surface, including
    // stickers on pieces crossing the straight color boundaries and seams.
    const centers:Record<string,[number,number]>={A:[.5,.5],B:[1.5,.5],C:[.5,1.5],D:[1.5,1.5],
      ...(six?{E:[1,1] as [number,number],F:[1,0] as [number,number]}:{})};
    expect(surface.diskCenters).toEqual(centers);
    const centerLifts=Object.entries(centers).flatMap(([color,point])=>images.map(image=>({
      color,point:new Vector3(...point,0).applyMatrix4(image.matrix),
    })));
    const homeFrames=copyFloatView(geometry.transforms());
    const stickers=scene.visualParts.flatMap((part,index)=>part.role==='port'?[{
      part,index,asset:assets.get(part.meshAssetId)!,frame:new Matrix4().fromArray(homeFrames,index*16),
    }]:[]);
    const catalog=JSON.parse(source).catalog;
    for(const {part,asset,frame} of stickers){
      expect(part.materialBindingId).toBe(catalog.pieces[part.pieceId].portLabels[part.portId!]);
      const matching=centerLifts.filter(center=>center.color===part.materialBindingId);
      for(let vertex=0;vertex<asset.vertexCount;vertex++){
        const p=new Vector3().fromArray(positions,asset.positionOffset+vertex*3).applyMatrix4(frame);
        const nearest=Math.min(...centerLifts.map(center=>p.distanceToSquared(center.point)));
        expect(Math.min(...matching.map(center=>p.distanceToSquared(center.point))),
          `${part.visualPartId} vertex ${vertex} lies outside its solved color region`).toBeLessThanOrEqual(nearest+1e-6);
      }
    }
    // The four-disk square has one triangle per cone. For six disks, square
    // coordinates (u,v) have cuts at 1/4 and 3/4: E in the big middle square,
    // A-D in four rectangles, and F in all four identified corner squares.
    const colorSamples:[string,number,number][]=six?
      ['FBF','AED','FCF'].flatMap((row,j)=>[.125,.5,.875].map((u,i)=>{
        const v=[.125,.5,.875][j];
        return [row[i],1+u-v,u+v] as [string,number,number];
      })):[['A',.6,.6],['B',1.4,.6],['C',.6,1.4],['D',1.4,1.4]];
    const stickerImages=stickers.flatMap(sticker=>images.map(image=>({
      ...sticker,inverse:new Matrix4().multiplyMatrices(image.matrix,sticker.frame).invert(),
    })));
    for(const [color,x,y] of colorSamples){
      const found=stickerImages.filter(({asset,inverse})=>{
        const p=new Vector3(x,y,0).applyMatrix4(inverse);
        for(let k=asset.indexOffset;k<asset.indexOffset+asset.indexCount;k+=3)
          if(insideTriangle(p,indices[k],indices[k+1],indices[k+2],asset.positionOffset))return true;
        return false;
      });
      expect(found.map(({part})=>part.materialBindingId),`Solved region at ${x},${y}`).toEqual([color]);
    }
    const bodyIndices=new Map(bodies.map(({part,index})=>[part.pieceId,index]));
    for(const operation of six?['A','A2','E','F']:['A','A2','C']){
      geometry.setStateJSON(session.stateText());
      const planned=session.plan({operation,parameters:{}});
      expect(JSON.parse(geometry.prepareAnimationJSON(planned.transitionRecord!)).status).toBe('Prepared');
      for(const progress of [0,.37,.999]){
        geometry.sample(progress);
        const frames=copyFloatView(geometry.transforms());
        if(progress===.37)for(const {part,index} of stickers){
          const body=bodyIndices.get(part.pieceId)!;
          expect(frames.slice(index*16,index*16+16),`${part.visualPartId} must travel with its piece`)
            .toEqual(frames.slice(body*16,body*16+16));
        }
        const instances=bodies.flatMap(({part,index})=>images.map(image=>({
          inverse:new Matrix4().multiplyMatrices(image.matrix,new Matrix4().fromArray(frames,index*16)).invert(),
          asset:assets.get(part.meshAssetId)!,
        })));
        let checked=0;
        for(let i=0;i<14;i++)for(let j=0;j<14;j++){
          const p=new Vector3((i+.173)/7,(j+.319)/7,0);
          if(!insidePolygon(polygon,p.x,p.y))continue;
          // The sampled polygonal arcs approximate the actual circular cut.
          // Exclude the narrow chord-error band at the slipping interface.
          const center=surface.diskCenters![operation.charAt(0)],radius=surface.cutRadius!;
          if(images.some(image=>Math.abs(p.distanceTo(new Vector3(...center,0).applyMatrix4(image.matrix))-radius)<.004))continue;
          let count=0;
          for(const {inverse,asset} of instances){
            const local=p.clone().applyMatrix4(inverse);
            for(let k=asset.indexOffset;k<asset.indexOffset+asset.indexCount;k+=3){
              if(insideTriangle(local,indices[k],indices[k+1],indices[k+2],asset.positionOffset)){
                count++;break;
              }
            }
          }
          expect(count,`${operation} t=${progress} at ${p.x},${p.y}`).toBe(1);
          checked++;
        }
        expect(checked).toBeGreaterThan(70);
      }
    }
    const saved=session.save({views:['fundamental-domain','tiled-plane']});
    const restored=new KernelSession(module,source);
    try{expect(restored.load(saved).status).toBe('Loaded');expect(restored.snapshot().stateDigest).toBe(origin);}
    finally{restored.dispose();}
  }finally{geometry.delete();session.dispose();}
});

it('preserves the 384-state four-cone puzzle after authoring its geometric package',()=>{
  const {source}=JSON.parse(readFileSync('packages/flat-pillowcase/package.json','utf8'));
  const initial=source.domains.flatMap((domain:any)=>domain.pieces.map((piece:any)=>piece.initial)) as string[];
  const maps=['A','B','C','D'].map(operation=>source.domains.flatMap((domain:any)=>{
    const transport=domain.moves.find((move:any)=>move.id===operation).transport;
    return domain.pieces.map(()=>transport);
  }));
  const queue=[initial],depths=[0],seen=new Set([JSON.stringify(initial)]);
  for(let i=0;i<queue.length;i++)for(const map of maps){
    const next=queue[i].map((p,j)=>map[j][p]??p),key=JSON.stringify(next);
    if(!seen.has(key)){seen.add(key);queue.push(next);depths.push(depths[i]+1);}
    expect(seen.size).toBeLessThanOrEqual(384);
  }
  expect(seen.size).toBe(384);
  expect(Math.max(...depths)).toBe(12);
});
