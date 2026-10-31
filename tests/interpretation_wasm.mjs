// Runs outside the repository with only copied SDK artifacts and fixture bytes.
import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [directory,entry,reference] = process.argv.slice(2);
const read = name => readFile(join(directory,name),'utf8');
const hash = text => createHash('sha256').update(text).digest('hex');
const canonical = value => {
  if (Array.isArray(value)) return '['+value.map(canonical).join(',')+']';
  if (value && typeof value === 'object') return '{'+Object.keys(value).sort().map(k=>JSON.stringify(k)+':'+canonical(value[k])).join(',')+'}';
  return JSON.stringify(value);
};
const seal = (doc,key) => {delete doc[key];doc[key]=hash(canonical(doc));};
function near(a,b,path='') {
  if (typeof a==='number' && typeof b==='number') assert.ok(Number.isFinite(a) && Math.abs(a-b)<=1e-8*(1+Math.abs(b)),path);
  else if (Array.isArray(a) && Array.isArray(b)) {assert.equal(a.length,b.length,path);a.forEach((x,i)=>near(x,b[i],`${path}/${i}`));}
  else if (a && b && typeof a==='object' && typeof b==='object') {assert.deepEqual(Object.keys(a).sort(),Object.keys(b).sort(),path);Object.keys(a).forEach(k=>near(a[k],b[k],`${path}/${k}`));}
  else assert.deepEqual(a,b,path);
}
function pose(scene,frame) {
  const values=Array.from(frame.isometries());
  return Object.fromEntries(scene.visualParts.map((p,i)=>[p.visualPartId,values.slice(i*16,(i+1)*16)]));
}
function meshes(geometry,scene) {
  const positions=Array.from(geometry.positions()), normals=Array.from(geometry.normals()),indices=Array.from(geometry.indices());
  return Object.fromEntries(scene.meshAssets.map(a=>[a.id,{positions:positions.slice(a.positionOffset,a.positionOffset+a.vertexCount*3),
    normals:normals.slice(a.positionOffset,a.positionOffset+a.vertexCount*3),indices:indices.slice(a.indexOffset,a.indexOffset+a.indexCount)}]));
}
function observe(module,text,manifest,envelope,payload,algorithm) {
  const session=module.createArtifactSession(text);assert.ok(session);session.lightweightResults(true);let geometry;
  try {
    const initial=JSON.parse(session.stateSnapshotJSON());
    geometry=module.createArtifactGeometryWithPayload(session,manifest,envelope,payload);assert.ok(geometry);
    const scene=JSON.parse(geometry.sceneJSON());assert.equal(scene.realizationDigest,JSON.parse(envelope).realizationDigest);
    let result;
    if (!algorithm) for (const request of JSON.parse(text).bindings.requests) {
      const trial=JSON.parse(session.runJSON(request.id,'transactional',session.revision()));
      if (trial.status==='Committed') {algorithm=request.id;result=trial;break;}
    }
    else result=JSON.parse(session.runJSON(algorithm,'transactional',session.revision()));
    assert.equal(result?.status,'Committed',JSON.stringify(result)?.slice(0,1000));delete result.transitionRecords;
    const frames=result.transitions.map(record=>{
      const animation=geometry.animate(JSON.stringify(record));
      try {
        const samples={};for (const t of [0,.5,1]) {const frame=animation.evaluate(t);try{samples[String(t*100)]=pose(scene,frame);}finally{frame.delete();}}
        const later=animation.evaluate(.9);later.delete();const again=animation.evaluate(.5);
        try{assert.deepEqual(pose(scene,again),samples['50']);}finally{again.delete();}
        const forged=structuredClone(record);delete forged.request.choices;assert.throws(()=>geometry.animate(JSON.stringify(forged)));
        return samples;
      } finally{animation.delete();}
    });
    const save=JSON.parse(session.saveJSON()),state=session.stateJSON();
    const restored=module.createSessionFromDefinition(session);assert.ok(restored);
    try{assert.equal(JSON.parse(restored.loadJSON(JSON.stringify(save))).status,'Loaded');assert.equal(restored.stateJSON(),state);}finally{restored.delete();}
    assert.equal(JSON.parse(session.undoJSON(session.revision())).status,'Committed');
    assert.equal(JSON.parse(session.redoJSON(session.revision())).status,'Committed');assert.equal(session.stateJSON(),state);
    const before=session.stateSnapshotJSON(),bad=structuredClone(save);bad.logical.executionDigest='0'.repeat(64);
    assert.equal(JSON.parse(session.loadJSON(JSON.stringify(bad))).status,'Invalid');assert.equal(session.stateSnapshotJSON(),before);
    return {algorithm,report:{artifactInfo:JSON.parse(module.artifactInfoJSON()),initial,result,save,frames,parts:scene.visualParts.length},scene,meshes:meshes(geometry,scene)};
  } finally{geometry?.delete();session.delete();}
}
const {createArtifactKernel}=await import(pathToFileURL(entry));
const module=await createArtifactKernel();
for(const name of ['compilePuzzleJSON','cubeModuleJSON','Puzzle','compileExecutionJSON'])assert.equal(typeof module[name],'undefined');
const text=await read('abstract.json'),manifest=await read('interpretation.json'),envelope=await read('realization.json'),payload=await read('geometry.json');
const {algorithm:preferred}=JSON.parse(await read('case.json'));
const observed=observe(module,text,manifest,envelope,payload,preferred);
const {default:createReference}=await import(pathToFileURL(reference));const full=await createReference();
const sourceText=await read('source-package.json');let sourceExpected;
const source=new full.Session(sourceText);source.lightweightResults(true);
const geometry=full.createGeometryDefinition(source,await read('source-realization.json'));assert.ok(geometry);
try{
  const definition=JSON.parse(source.definitionJSON()),types=Object.fromEntries(definition.pieceTypes.map(t=>[t.id,t.placementDomainId]));
  const domains=Object.fromEntries(definition.pieces.map(p=>[p.id,types[p.type]]));
  const qualify=s=>({placementOf:Object.fromEntries(Object.entries(s.placementOf).map(([p,q])=>[p,`${domains[p]}/${q}`])),mechanism:s.mechanism});
  const result=JSON.parse(source.runJSON(observed.algorithm,'transactional',source.revision()));assert.equal(result.status,'Committed');
  sourceExpected=result.snapshot.state;assert.deepEqual(qualify(sourceExpected),observed.report.result.snapshot.state);
  const scene=JSON.parse(geometry.sceneJSON());assert.deepEqual(scene.space,observed.scene.space);near(meshes(geometry,scene),observed.meshes,'meshes');
  assert.equal(result.transitions.length,observed.report.result.transitions.length);
  result.transitions.forEach((record,i)=>{
    const actual=observed.report.result.transitions[i];assert.deepEqual(qualify(record.beforeState),actual.beforeState);assert.deepEqual(qualify(record.afterState),actual.afterState);
    const expectedActions=record.pieceActions.map(a=>({pieceId:a.pieceId,from:`${domains[a.pieceId]}/${a.from}`,to:`${domains[a.pieceId]}/${a.to}`,event:a.event})).sort((a,b)=>a.pieceId.localeCompare(b.pieceId));
    assert.deepEqual(actual.pieceActions.map(({pieceId,from,to,event})=>({pieceId,from,to,event})).sort((a,b)=>a.pieceId.localeCompare(b.pieceId)),expectedActions);
    const animation=geometry.animate(JSON.stringify(record));try{for(const t of [0,.5,1]){const frame=animation.evaluate(t);try{near(pose(scene,frame),observed.report.frames[i][String(t*100)],'source poses');}finally{frame.delete();}}}finally{animation.delete();}
  });
}finally{geometry.delete();source.delete();}
// The source adapter also accepts request IDs independent of local move IDs.
let aliasAlgorithm;
if (JSON.parse(text).bindings.puzzleId==='cube3' && JSON.parse(payload).geometry.kind==='cube-euclidean') {
  const input=JSON.parse(sourceText),names={};
  input.source.requests.forEach((r,i)=>{names[r.id]='alias_'+String.fromCharCode(97+i);r.id=names[r.id];});
  input.catalog.requests=Object.fromEntries(Object.entries(input.catalog.requests).map(([id,metadata])=>[names[id],{...metadata,inverse:names[metadata.inverse]}]));
  const aliased=new full.Session(JSON.stringify(input));aliased.lightweightResults(true);
  try{
    aliasAlgorithm=observed.report.result.transitions.map(record=>names[record.request.operation]).join(' ');
    const result=JSON.parse(aliased.runJSON(aliasAlgorithm,'transactional',aliased.revision()));assert.equal(result.status,'Committed');assert.deepEqual(result.snapshot.state,sourceExpected);
    const state=aliased.stateJSON();assert.equal(JSON.parse(aliased.undoJSON(aliased.revision())).status,'Committed');assert.equal(JSON.parse(aliased.redoJSON(aliased.revision())).status,'Committed');assert.equal(aliased.stateJSON(),state);
    const realization=JSON.parse(await read('source-realization.json'));realization.compatibleDefinitionDigest=JSON.parse(aliased.definitionJSON()).definitionDigest;
    await writeFile(join(directory,'alias-source.json'),JSON.stringify(input));await writeFile(join(directory,'alias-realization.json'),JSON.stringify(realization));
  }finally{aliased.delete();}
}
// Independent movement names, choice names, events, domain IDs and geometric groups.
const doc=JSON.parse(text),m=JSON.parse(manifest),e=JSON.parse(envelope),p=JSON.parse(payload),requests={};
doc.bindings.requests.forEach((r,i)=>{requests[r.id]='turn_'+i.toString(26).split('').map(c=>String.fromCharCode(97+parseInt(c,26))).join('');r.id=requests[r.id];r.profiles.forEach((profile,j)=>{profile.id=`candidate_${i}_${j}`;});});
doc.bindings.choiceSlots=doc.bindings.choiceSlots.map((_,i)=>`slot_${i}`);
doc.bindings.choiceValues=doc.bindings.choiceValues.map((row,i)=>row.map((_,j)=>`choice_${i}_${j}`));
doc.bindings.events=doc.bindings.events.map((_,i)=>`event_${i}`);seal(doc,'executionDigest');m.executionDigest=doc.executionDigest;
const domains={};m.domains.forEach((domain,i)=>{domains[domain.id]=`scope_${i}`;domain.id=domains[domain.id];});seal(m,'interpretationDigest');
for(const key of ['componentsByDomain','modelByDomain','domains'])if(p.geometry[key])p.geometry[key]=Object.fromEntries(Object.entries(p.geometry[key]).map(([key,value])=>[domains[key],value]));
p.requestBindings.forEach(binding=>{binding.group='renamed_'+binding.group;});p.geometry.compatibleDefinitionDigest=doc.executionDigest;
e.interpretationDigest=m.interpretationDigest;e.payload.sha256=hash(JSON.stringify(p));seal(e,'realizationDigest');
const renamed=observe(module,JSON.stringify(doc),JSON.stringify(m),JSON.stringify(e),JSON.stringify(p),observed.report.result.transitions.map(record=>requests[record.request.operation]).join(' '));
assert.deepEqual(renamed.report.result.snapshot.state,observed.report.result.snapshot.state);near(renamed.report.frames,observed.report.frames,'opaque poses');
await writeFile(join(directory,'wasm-report.json'),JSON.stringify({algorithm:observed.algorithm,aliasAlgorithm,report:observed.report}));
