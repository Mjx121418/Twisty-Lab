// Independent native/WASM consumers. Run heavy steps sequentially under 4 GiB.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFile, cp, mkdtemp, mkdir, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import Ajv from 'ajv';
import { canonicalIntegerJSON } from '../tests/architecture/contracts.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const published = join(root, 'build/kernel');
const temporary = await mkdtemp(join(tmpdir(), 'twisty-artifact-'));
const run = (file, args) => execFileSync(file, args, { cwd: root, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
const hash = text => createHash('sha256').update(text).digest('hex');
const read = file => readFile(join(root, file), 'utf8');
const seal = (doc, key) => { delete doc[key]; doc[key] = hash(canonicalIntegerJSON(doc)); };
function near(a, b, path = '') {
  if (typeof a === 'number' && typeof b === 'number') assert.ok(Number.isFinite(a) && Math.abs(a-b) <= 1e-8 * (1+Math.abs(b)), path);
  else if (Array.isArray(a) && Array.isArray(b)) {
    assert.equal(a.length, b.length, path); a.forEach((x,i) => near(x,b[i],`${path}/${i}`));
  } else if (a && b && typeof a === 'object' && typeof b === 'object') {
    assert.deepEqual(Object.keys(a).sort(),Object.keys(b).sort(),path); Object.keys(a).forEach(k => near(a[k],b[k],`${path}/${k}`));
  } else assert.deepEqual(a,b,path);
}
function pose(geometry, frame) {
  const values = Array.from(frame.isometries());
  return Object.fromEntries(JSON.parse(geometry.sceneJSON()).visualParts.map((p,i) => [p.visualPartId, values.slice(i*16,(i+1)*16)]));
}
function observe(module, text, manifest, realization, algorithm) {
  const session = module.createArtifactSession(text); assert.ok(session); session.lightweightResults(true);
  let geometry;
  try {
    const initial = JSON.parse(session.stateSnapshotJSON());
    geometry = module.createArtifactGeometryDefinition(session, manifest, realization); assert.ok(geometry);
    const scene = JSON.parse(geometry.sceneJSON());
    assert.equal(scene.realizationDigest, hash(realization));
    const result = JSON.parse(session.runJSON(algorithm, 'transactional', session.revision()));
    assert.equal(result.status, 'Committed'); delete result.transitionRecords;
    const frames = result.transitions.map(record => {
      const animation = geometry.animate(JSON.stringify(record));
      try {
        return Object.fromEntries([0,.5,1].map(t => {
          const f = animation.evaluate(t); try { return [String(t*100),pose(geometry,f)]; } finally { f.delete(); }
        }));
      } finally { animation.delete(); }
    });
    const save = JSON.parse(session.saveJSON()); assert.equal(save.schemaVersion, 3);
    const restored = module.createSessionFromDefinition(session); assert.ok(restored);
    try {
      assert.equal(JSON.parse(restored.loadJSON(JSON.stringify(save))).status,'Loaded');
      assert.equal(restored.stateJSON(),session.stateJSON());
    } finally { restored.delete(); }
    const state = session.stateJSON();
    assert.equal(JSON.parse(session.undoJSON(session.revision())).status,'Committed');
    assert.equal(JSON.parse(session.redoJSON(session.revision())).status,'Committed');
    assert.equal(session.stateJSON(),state);
    const before = session.stateSnapshotJSON();
    const bad = structuredClone(save); bad.logical.executionDigest = '0'.repeat(64);
    assert.equal(JSON.parse(session.loadJSON(JSON.stringify(bad))).status,'Invalid');
    assert.equal(session.stateSnapshotJSON(),before);
    const forged = structuredClone(result.transitions[0]); delete forged.request.choices;
    assert.throws(() => geometry.animate(JSON.stringify(forged)));
    return { artifactInfo:JSON.parse(module.artifactInfoJSON()),initial,result,save,frames,parts:scene.visualParts.length };
  } finally { geometry?.delete(); session.delete(); }
}
try {
  run('cmake',['--build','build/native','--parallel','1','--target','artifact_consumer','artifact_tests','twisty_language']);
  console.log(run('build/native/artifact_tests',[]).trim());
  run(process.execPath,['scripts/build-artifact-examples.mjs']);
  const installed = join(temporary,'native'); run('cmake',['--install','build/native','--prefix',installed]);
  const consumer = join(temporary,'consumer'); await cp(join(published,'examples/artifact'),consumer,{recursive:true});
  run('cmake',['-S',consumer,'-B',join(consumer,'build'),'-G','Ninja',`-DCMAKE_PREFIX_PATH=${installed}`]);
  run('cmake',['--build',join(consumer,'build'),'--parallel','1']);
  const commands = run('ninja',['-C',join(consumer,'build'),'-t','commands']);
  assert.ok(!/libpuzzle_(?:language|execution|session|geometry)\.a/.test(commands),'Artifact consumer linked a source adapter');
  const js = join(temporary,'javascript'); await mkdir(join(js,'generated'),{recursive:true});
  // Deliberately omit the full SDK entry, full WASM, source examples and generators.
  for (const file of ['artifact.js','artifact.d.ts']) await cp(join(published,file),join(js,file));
  for (const file of ['twisty-artifact.mjs','twisty-artifact.wasm','twisty-artifact.d.mts'])
    await cp(join(published,'generated',file),join(js,'generated',file));
  const {createArtifactKernel} = await import(pathToFileURL(join(js,'artifact.js')));
  const module = await createArtifactKernel();
  for (const absent of ['compilePuzzleJSON','compileExecutionJSON','cubeModuleJSON','gearModuleJSON','cubeArtifactBundleJSON','Puzzle'])
    assert.equal(typeof module[absent],'undefined',`${absent} leaked into the artifact module`);
  const {default:createFullModule} = await import(new URL('../kernel/generated/twisty.mjs',import.meta.url));
  const full = await createFullModule();
  const schemas = new Ajv({strict:false,allErrors:true});
  for (const name of ['compact-v2','artifact-common','abstract-artifact','interpretation','artifact-realization','state','session-v3','artifact-witness'])
    schemas.addSchema(JSON.parse(await readFile(join(published,`schemas/${name}.schema.json`),'utf8')));
  const schema = (id,value) => { const validator=schemas.getSchema(`urn:twisty:schema:${id}`); assert.ok(validator); assert.ok(validator(value),schemas.errorsText(validator.errors)); };
  for (const [name,algorithm] of [['cube3',"R U R' U'"],['bandaged',"U R R' U'"]]) {
    const dir=join(consumer,'data',name), text=await readFile(join(dir,'abstract.json'),'utf8');
    const manifest=await readFile(join(dir,'interpretation.json'),'utf8'), realization=await readFile(join(dir,'realization.json'),'utf8');
    schema('abstract-artifact:1',JSON.parse(text)); schema('interpretation:1',JSON.parse(manifest)); schema('artifact-realization:1',JSON.parse(realization));
    const native=JSON.parse(run(join(consumer,'build/artifact_consumer'),[join(dir,'abstract.json'),join(dir,'interpretation.json'),join(dir,'realization.json'),algorithm]));
    const wasm=observe(module,text,manifest,realization,algorithm);
    const {frames,...logical}=wasm,{frames:reference,...nativeLogical}=native;
    assert.deepEqual(logical,nativeLogical); near(frames,reference);
    schema('session:3',wasm.save); wasm.result.transitions.forEach(t=>schema('artifact-witness:2',t));
    const source=new full.Session(await read(`packages/${name}/package.json`)); source.lightweightResults(true);
    const oldGeometry=full.createGeometryDefinition(source,await read(`packages/${name}/cube-euclidean.json`)); assert.ok(oldGeometry);
    try {
      const def=JSON.parse(source.definitionJSON()),domains=Object.fromEntries(def.pieces.map(p=>[p.id,def.pieceTypes.find(t=>t.id===p.type).placementDomainId]));
      const qualify=state=>({placementOf:Object.fromEntries(Object.entries(state.placementOf).map(([p,q])=>[p,`${domains[p]}/${q}`])),mechanism:state.mechanism});
      const result=JSON.parse(source.runJSON(algorithm,'transactional',source.revision()));
      assert.deepEqual(qualify(result.snapshot.state),wasm.result.snapshot.state);
      result.transitions.forEach((record,i)=>{
        assert.deepEqual(qualify(record.beforeState),wasm.result.transitions[i].beforeState);
        assert.deepEqual(qualify(record.afterState),wasm.result.transitions[i].afterState);
        const actions=record.pieceActions.map(a=>({pieceId:a.pieceId,from:`${domains[a.pieceId]}/${a.from}`,to:`${domains[a.pieceId]}/${a.to}`,event:a.event})).sort((a,b)=>a.pieceId.localeCompare(b.pieceId));
        const actual=wasm.result.transitions[i].pieceActions.map(({pieceId,from,to,event})=>({pieceId,from,to,event})).sort((a,b)=>a.pieceId.localeCompare(b.pieceId));
        assert.deepEqual(actions,actual);
        const animation=oldGeometry.animate(JSON.stringify(record));
        try { for (const t of [0,.5,1]) {const frame=animation.evaluate(t);try {near(pose(oldGeometry,frame),wasm.frames[i][String(t*100)]);}finally{frame.delete();}} }
        finally {animation.delete();}
      });
    } finally {oldGeometry.delete();source.delete();}
    // Prove movement/event/slot spellings are opaque, with unchanged mathematics.
    const renamed=JSON.parse(text), m=JSON.parse(manifest),r=JSON.parse(realization),names={};
    renamed.bindings.requests.forEach((request,i)=>{ names[request.id]=`turn_${String.fromCharCode(97+i)}`;request.id=names[request.id]; });
    renamed.bindings.choiceValues=renamed.bindings.choiceValues.map((row,slot)=>row.map((_,choice)=>`choice_${slot}_${choice}`));
    renamed.bindings.requests.forEach((request,i)=>request.profiles.forEach((profile,j)=>{profile.id=`candidate_${i}_${j}`;}));
    renamed.bindings.choiceSlots=renamed.bindings.choiceSlots.map((_,i)=>`slot_${String.fromCharCode(97+i)}`);
    renamed.bindings.events=renamed.bindings.events.map((_,i)=>`motion_${String.fromCharCode(97+i)}`);
    seal(renamed,'executionDigest'); m.executionDigest=renamed.executionDigest;seal(m,'interpretationDigest');
    r.compatibleDefinitionDigest=renamed.executionDigest;r.compatibleInterpretationDigest=m.interpretationDigest;
    r.explicitCubeTracks=Object.fromEntries(Object.entries(r.explicitCubeTracks).map(([id,track])=>[names[id],track]));
    const opaque=observe(module,JSON.stringify(renamed),JSON.stringify(m),JSON.stringify(r),algorithm.split(' ').map(id=>names[id]).join(' '));
    assert.deepEqual(opaque.result.snapshot.state,wasm.result.snapshot.state);near(opaque.frames,wasm.frames);
    const bad=JSON.parse(text);bad.program.actionRecords[0].map=4294967294;seal(bad,'executionDigest');
    assert.equal(JSON.parse(module.validateArtifactJSON(JSON.stringify(bad))).status,'Invalid');
    const duplicate='{"kind":"twisty-abstract-artifact",'+text.slice(text.indexOf('{')+1);
    assert.equal(JSON.parse(module.validateArtifactJSON(duplicate)).status,'Invalid');
    console.log(`${name}: installed compiler-free native/WASM session, replay, exact witnesses, geometry and opaque movement IDs passed.`);
  }
} finally { await rm(temporary,{recursive:true,force:true}); }
