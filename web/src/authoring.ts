// This chunk and its compiler are loaded only for explicit authoring actions.
import { createAuthoring, compileRuntimeBundle, migrateSourceSession } from '../../kernel/authoring';
import type { RuntimeBundle } from '../../kernel/runtime';
import wasmURL from '../../kernel/generated/twisty.wasm?url';
import gearSuppliedText from '../../packages/gear-extreme/gear-extreme-supplied-euclidean.json?raw';
import gearDiagramText from '../../packages/gear/gear-port-diagram.json?raw';
import gearSphereText from '../../packages/gear/gear-spherical.json?raw';
const sources=import.meta.glob<string>('../../packages/*/package.json',{query:'?url',import:'default',eager:true});
const wrappers=import.meta.glob<string>('../../packages/gear*/source.json',{query:'?url',import:'default',eager:true});
let instance:ReturnType<typeof createAuthoring>|undefined;
const authoring=()=>instance??=createAuthoring({wasmURL});
async function readAsset(url:string|undefined):Promise<string>{
  if(!url)throw new Error('The matching source definition is unavailable.');
  const response=await fetch(url);if(!response.ok)throw new Error(`Cannot load authoring source (${response.status}).`);return response.text();
}
export async function presetSource(id:string):Promise<string>{return readAsset(sources[`../../packages/${id}/package.json`]);}
export async function gearConfiguration(id:string):Promise<{kind:string;formatVersion:number;puzzleId:string;gearedEdges:string[]}>{
  return JSON.parse(await readAsset(wrappers[`../../packages/${id}/source.json`]));
}
export async function compileSource(source:string,views:readonly unknown[]=[]):Promise<RuntimeBundle>{
  return compileRuntimeBundle(await authoring(),source,views);
}
export async function compileGear(edges:readonly string[],id='gear-custom-experimental-v1',viewId='gear-custom'):Promise<{bundle:RuntimeBundle;source:string}>{
  const source=JSON.stringify({kind:'relational-gear-prototypes',formatVersion:1,puzzleId:id,gearedEdges:edges});
  const module=await authoring();
  const session=new module.Session(source);
  let digest:string,example:string;
  try{
    digest=JSON.parse(session.stateSnapshotJSON()).definitionDigest;
    const rightLegal=JSON.parse(session.planJSON(JSON.stringify({operation:'R',parameters:{}}),session.stateJSON())).status==='Legal';
    example=edges.length===0?"Rq Uq Rq' Uq'":rightLegal?"R R'":"Rq Rq'";
  }finally{session.delete();}
  const templates=[gearSuppliedText,gearDiagramText,gearSphereText].map(text=>{
    const geometry=JSON.parse(text);geometry.compatibleDefinitionDigest=digest;
    geometry.id=geometry.kind.endsWith('-spherical')?`${viewId}-spherical-v1`:geometry.kind.endsWith('-port-diagram')?`${viewId}-diagram-v1`:`${viewId}-supplied-articulated-euclidean-v1`;return geometry;
  });
  const bundle=compileRuntimeBundle(module,source,templates);
  if(bundle.presentation.display)bundle.presentation.display.defaultAlgorithm=example;
  return{bundle,source};
}
export async function migrateSave(source:string,save:string):Promise<string>{return migrateSourceSession(await authoring(),source,save);}
