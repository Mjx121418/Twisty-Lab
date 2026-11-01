import { readFileSync } from 'node:fs';
import { beforeAll, expect, it } from 'vitest';
import { createKernel, KernelSession, type MainModule } from '../../kernel/index';
import { compileRuntimeBundle } from '../../kernel/authoring';

const read=(path:string)=>readFileSync(path,'utf8');
const source=read('packages/cube3/package.json');
const views=['cube-euclidean','cube-spherical'].map(id=>JSON.parse(read(`packages/cube3/${id}.json`)));
let module: MainModule;
beforeAll(async()=>{module=await createKernel();});
it('reuses an immutable authoring definition without exporting or mutating its history',()=>{
  const session=new KernelSession(module,source);
  const fresh=compileRuntimeBundle(module,source,views);
  let reused: ReturnType<typeof compileRuntimeBundle>;
  try {
    session.move('R');const before=session.save();
    reused=compileRuntimeBundle(module,session.native,views);
    expect(reused).toEqual(fresh);expect(session.save()).toBe(before);
    for(const view of reused.views)
      expect(view.payload).toBe(read(`runtime-puzzles/cube3/realizations/${view.presentation.kind}/geometry.json`));
  }finally{session.dispose();}
  expect(reused.abstract.executionDigest).toBe(fresh.abstract.executionDigest);
});
it('retains per-view pin validation when sharing an authoring context',()=>{
  const session=new KernelSession(module,source);
  try {
    const before=session.save(),bad=structuredClone(views);
    bad[1].compatibleDefinitionDigest='0'.repeat(64);
    expect(()=>compileRuntimeBundle(module,session.native,bad)).toThrow('another source definition');
    expect(session.save()).toBe(before);
  }finally{session.dispose();}
});
it('rejects an artifact session as an authoring context',()=>{
  const native=module.createArtifactSession(read('runtime-puzzles/cube3/abstract.json'))!;
  try{expect(()=>compileRuntimeBundle(module,native,views)).toThrow('source-compiled');}
  finally{native.delete();}
});
