// Installed runtime-only consumers, controller contracts and optional authoring.
// Builds and observations are deliberately sequential under the 4 GiB budget.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {cp,mkdtemp,readFile,readdir,rm,writeFile,mkdir,stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import Ajv from 'ajv';
import {canonicalIntegerJSON} from '../tests/architecture/contracts.mjs';
import {checkRuntimeData} from '../tools/authoring/runtime-data.mjs';
import {readInventory} from './puzzle-data.mjs';
const root=fileURLToPath(new URL('../',import.meta.url));
const temporary=await mkdtemp(join(tmpdir(),'twisty-runtime-'));
const run=(command,args,options={})=>{
  try{return execFileSync(command,args,{cwd:root,encoding:'utf8',maxBuffer:16*1024*1024,...options});}
  catch(error){throw new Error(`${command} failed: ${String(error.stderr||error.stdout||error.code||error.message).slice(-6000)}`);}
};
const read=path=>readFile(path,'utf8');
const seal=(document,key)=>{delete document[key];document[key]=createHash('sha256').update(canonicalIntegerJSON(document)).digest('hex');};
const near=(a,b)=>{assert.equal(a.length,b.length);a.forEach((x,i)=>assert.ok(Number.isFinite(x)&&Math.abs(x-b[i])<1e-7*(1+Math.abs(x)),`pose ${i}`));};
async function files(directory,prefix='') {
  const result=[];
  for(const item of await readdir(directory,{withFileTypes:true})) {
    const name=prefix+item.name;
    if(item.isDirectory())result.push(...await files(join(directory,item.name),name+'/'));else result.push(name);
  }
  return result;
}
try {
  await checkRuntimeData();
  try {
    const memory=Number(await read('/sys/fs/cgroup/memory.current'));
    console.log(`Runtime checks: cgroup ${(memory/1024**3).toFixed(2)} GiB; builds use one job.`);
  } catch(error) {if(error.code!=='ENOENT')throw error;}
  const build=join(root,'build/runtime-native');
  run('cmake',['-S',root,'-B',build,'-G','Ninja','-DTWISTY_BUILD_AUTHORING=OFF','-DBUILD_TESTING=OFF','-DCMAKE_BUILD_TYPE=Debug']);
  run('cmake',['--build',build,'--parallel','1'],{stdio:'inherit'});
  const wasmCommands=run('ninja',['-C',join(root,'build/wasm'),'-t','commands','twisty_artifact_wasm']);
  assert.ok(!/libpuzzle_(?:language|execution|session|geometry|authoring_geometry|symmetry)\.a/.test(wasmCommands),'Runtime WASM links authoring libraries');
  const installed=join(temporary,'native');run('cmake',['--install',build,'--prefix',installed]);
  const nativeFiles=await files(installed);
  assert.ok(!nativeFiles.some(f=>/authoring|libpuzzle_(?:language|symmetry|execution|session|geometry)\.a$|kernel\.hpp$/.test(f)),'Authoring files leaked into runtime installation');
  const exported=await read(join(installed,'lib/cmake/TwistyKernel/TwistyKernelTargets.cmake'));
  assert.ok(exported.includes('Twisty::runtime_kernel'));
  assert.ok(!/Twisty::(?:language|symmetry|execution["; ]|session["; ]|geometry["; ]|kernel["; ])/.test(exported));
  for(const name of nativeFiles.filter(name=>name.endsWith('.a'))) {
    const symbols=run('nm',['-C','-g',join(installed,name)],{maxBuffer:64*1024*1024});
    assert.ok(!/twisty::language::|twisty::Execution::from_source\(|twisty::Session::Session\(std::.*basic_string|compile_gear|cube_module|gear_module/.test(symbols),`Compiler/factory dependency in ${name}`);
  }
  const consumer=join(temporary,'consumer');await cp(join(root,'examples/runtime-native'),consumer,{recursive:true});
  run('cmake',['-S',consumer,'-B',join(consumer,'build'),'-G','Ninja',`-DCMAKE_PREFIX_PATH=${installed}`]);
  run('cmake',['--build',join(consumer,'build'),'--parallel','1']);
  // Copy only the minimal distribution out of the workspace. No source, full
  // module, Python generators or React/Three/Vite dependencies are available.
  const javascript=join(temporary,'javascript');
  await cp(join(root,'build/runtime-sdk'),javascript,{recursive:true,filter:path=>!path.includes('/data/')&&!path.endsWith('/data')});
  const jsFiles=await files(javascript);
  assert.ok(!jsFiles.some(name=>/(?:^|\/)(?:index|authoring)\.(?:js|d\.ts)$|twisty\.(?:mjs|wasm)|\.pzl\.json|package\.json$/.test(name)&&name!=='package.json'));
  const manifest=JSON.parse(await read(join(javascript,'package.json')));assert.equal(manifest.name,'@twisty/runtime');
  const declarations=join(javascript,'consumer.ts');
  await writeFile(declarations,`import {createRuntime,RuntimeSession,RuntimeController,GeometryInstance,geometryBackends,type RuntimeDocuments,type RuntimeGeometryInput,type Snapshot} from './runtime.js';
function consume(module:Awaited<ReturnType<typeof createRuntime>>,documents:RuntimeDocuments,input:RuntimeGeometryInput) {
  const registryVersion:1=geometryBackends(module).registryVersion;
  const session=new RuntimeSession(module,documents),controller=new RuntimeController(module,session);
  const geometry=new GeometryInstance(module,session.native,input);
  const snapshot:Snapshot=session.snapshot();
  const execution:string=snapshot.executionDigest,view:string=geometry.scene.realizationDigest;
  // @ts-expect-error source identity is absent from runtime snapshots
  const legacy:string=snapshot.definitionDigest;
  // @ts-expect-error the runtime module has no source compiler
  module.compileExecutionJSON('{}');
  geometry.dispose();controller.dispose();session.dispose();return [execution,view,registryVersion];
}
`);
  run(join(root,'node_modules/.bin/tsc'),['--ignoreConfig','--noEmit','--strict','--skipLibCheck','--target','ES2022','--module','NodeNext',declarations]);
  const start=performance.now();
  const {createRuntime,RuntimeSession,RuntimeController,GeometryInstance,geometryBackends,resolveMovementAlias}=await import(pathToFileURL(join(javascript,'runtime.js')));
  const module=await createRuntime();const initializationMs=performance.now()-start;
  for(const absent of ['Puzzle','compilePuzzleJSON','compileExecutionJSON','cubeModuleJSON','gearModuleJSON','cubeSourceJSON','gearSourceJSON','runtimeArtifactsJSON','migrateSourceSessionJSON'])
    assert.equal(typeof module[absent],'undefined',`${absent} leaked into runtime WASM`);
  assert.throws(()=>new module.Session('{}'),'Source constructor leaked into runtime WASM');
  assert.equal(JSON.parse(module.artifactInfoJSON()).runtimeAPIVersion,1);
  const backendInfo=geometryBackends(module);
  assert.equal(backendInfo.registryVersion,1);
  assert.equal(new Set(backendInfo.backends.map(b=>b.id)).size,backendInfo.backends.length);
  const validatePresentation=new Ajv({strict:false}).compile(JSON.parse(await read(join(root,'schemas/presentation-artifact.schema.json'))));
  const validateControl=new Ajv({strict:false}).compile(JSON.parse(await read(join(root,'schemas/control-artifact.schema.json'))));
  const inventory=await readInventory();let views=0;const measurements=[];
  for(const puzzle of inventory.puzzles) {
    const data=join(temporary,'data',puzzle.id);await cp(join(root,'runtime-puzzles',puzzle.id),data,{recursive:true});
    const abstract=await read(join(data,'abstract.json')),controls=await read(join(data,'controls.json')),presentation=await read(join(data,'presentation.json'));
    const presentationDoc=JSON.parse(presentation);assert.ok(validatePresentation(presentationDoc),JSON.stringify(validatePresentation.errors));
    if(puzzle.id.startsWith('flat-')||puzzle.id.startsWith('icosahedron-'))
      assert.deepEqual(presentationDoc.display.familyOrder,[...new Set(presentationDoc.operations.map(op=>op.family))].sort(),'Non-cube family order changed');
    const parsed=JSON.parse(controls);assert.ok(validateControl(parsed),JSON.stringify(validateControl.errors));
    assert.equal(JSON.parse(module.validateRuntimeJSON(abstract,controls)).status,'Valid');
    const before=performance.now(),session=new RuntimeSession(module,{abstract,controls,presentation});session.lightweightResults();
    const loadMs=performance.now()-before;const controller=new RuntimeController(module,session);
    try {
      if(puzzle.id==='cube3') {
        const renamed=structuredClone(presentationDoc);
        renamed.display.operations.R={...renamed.display.operations.R,label:'Right cap',accessibleLabel:'Turn right cap',aliases:['right cap']};
        const labeled=new RuntimeSession(module,{abstract,controls,presentation:JSON.stringify(renamed)});
        const plain=structuredClone(presentationDoc);delete plain.display;
        const anonymous=new RuntimeSession(module,{abstract,controls,presentation:JSON.stringify(plain)});
        try {
          assert.equal(resolveMovementAlias(labeled.definition,'right cap'),'R');
          assert.deepEqual(labeled.move('R'),anonymous.move('R'));
          assert.equal(labeled.save(),anonymous.save());
        } finally {labeled.dispose();anonymous.dispose();}
        renamed.display.operations.R.aliases=['U'];
        assert.throws(()=>new RuntimeSession(module,{abstract,controls,presentation:JSON.stringify(renamed)}));
      }
      const initial=session.stateSnapshot();const legal=session.snapshot().legalRequests;assert.ok(legal.length);
      const request=legal[0];const algorithm=request.operation;
      const result=session.execute(request);assert.equal(result.status,'Committed');delete result.transitionRecords;
      const interpretation=await read(join(data,'interpretation.json'));
      for(const filename of puzzle.realizations) {
        const id=filename.slice(0,-5),directory=join(data,'realizations',id);
        const envelope=await read(join(directory,'realization.json')),payload=await read(join(directory,'geometry.json'));
        const backend=JSON.parse(envelope).backend;
        const support=backendInfo.backends.find(b=>b.id===backend.id&&b.version===backend.version);
        assert.ok(support?.envelopeVersions.includes(1),'Undiscoverable backend/version');
        assert.ok(JSON.parse(envelope).requiredCapabilities.every(cap=>support.capabilities.includes(cap)));
        if(puzzle.id==='icosahedron-152') {
          const hint=JSON.parse(await read(join(directory,'presentation.json')));
          const track=hint.rotationTracks.A,coordinates=hint.motionFamilies.families[track.family].coordinates;
          const angle=-(coordinates[track.to]-coordinates[track.from]+track.winding*2*Math.PI);
          assert.ok(Math.abs(presentationDoc.display.operations.A.angleDegrees[0]-120)<1e-10,'Explicit presentation angle was lost');
          assert.equal(presentationDoc.display.interaction,'relative');
          assert.ok(Math.abs(angle-2*Math.PI/3)<1e-12,'Authored relative-angle hint was lost at the runtime boundary');
        }
        const geometry=new GeometryInstance(module,session.native,{interpretation,envelope,payload});
        try {
          assert.equal(geometry.scene.executionDigest,initial.executionDigest);
          assert.equal(JSON.parse(geometry.definition.spaceJSON()).id,support.space);
          const endpoint=Array.from(geometry.isometries);
          geometry.prepare(JSON.stringify(result.transitions[0]),false);
          const poses=[0,.5,1].map(progress=>{geometry.evaluatePose(progress);return Array.from(geometry.isometries);});
          near(Array.from(geometry.isometries),endpoint);
          geometry.evaluatePose(0.5);geometry.evaluatePose(1);near(Array.from(geometry.isometries),endpoint);
          const forged=structuredClone(result.transitions[0]);forged.executionDigest='0'.repeat(64);
          assert.throws(()=>geometry.prepare(JSON.stringify(forged)));
          const native=JSON.parse(run(join(consumer,'build/runtime_distribution'),[join(data,'abstract.json'),join(data,'controls.json'),join(data,'interpretation.json'),join(directory,'realization.json'),join(directory,'geometry.json'),algorithm]));
          assert.deepEqual(native.result,result);assert.deepEqual(native.snapshot,session.stateSnapshot());
          assert.equal(native.poses.length,1);poses.forEach((sample,i)=>near(sample,native.poses[0][i]));
          assert.deepEqual(native.backends,backendInfo);
          assert.deepEqual(native.controls,controller.catalog());assert.equal(native.parts,geometry.scene.visualParts.length);assert.deepEqual(native.save,JSON.parse(session.save()));
          views++;
        } finally {geometry.dispose();}
      }
      const digest=session.stateSnapshot().stateDigest;assert.equal(session.undo().status,'Committed');assert.equal(session.stateSnapshot().stateDigest,initial.stateDigest);
      const save=session.save({views:['diagram','cube']});const restored=new RuntimeSession(module,session);
      try {assert.equal(restored.load(save).status,'Loaded');assert.equal(restored.snapshot().canRedo,true);assert.equal(restored.redo().status,'Committed');assert.equal(restored.stateSnapshot().stateDigest,digest);}finally{restored.dispose();}
      if(puzzle.id==='helicopter') {
        assert.deepEqual(controller.familiesForPieces(['corner/FLU','corner/FRU']).families,['UF']);
        assert.equal(controller.familyState('UF').current.UF,'a');const intent={family:'UF',targets:{UF:'d'}};
        assert.equal(controller.planTo(intent).status,'Planned');assert.equal(controller.turnTo(intent).status,'Committed');
        assert.equal(controller.familyState('UF').current.UF,'d');assert.equal(controller.turnTo(intent,'0').status,'StaleRevision');
        const reject=change=>{const document=structuredClone(parsed);change(document);seal(document,'controlDigest');assert.equal(JSON.parse(module.validateRuntimeJSON(abstract,JSON.stringify(document))).status,'Invalid');};
        reject(d=>d.contractVersion=99);reject(d=>d.executionDigest='0'.repeat(64));reject(d=>d.catalog.drivers[0].piece=4294967294);
        reject(d=>d.catalog.drivers[0].reader[0].stop=128);reject(d=>d.catalog.drivers[0].reader.push(d.catalog.drivers[0].reader[0]));
        reject(d=>d.catalog.families[0].routes[0].profile='missing');reject(d=>d.catalog.families[0].drivers=['missing']);reject(d=>d.catalog.families[0].routes.pop());
        const bad=structuredClone(parsed);bad.controlDigest='0'.repeat(64);assert.equal(JSON.parse(module.validateRuntimeJSON(abstract,JSON.stringify(bad))).status,'Invalid');
      }
      measurements.push({id:puzzle.id,loadMs});console.log(`${puzzle.id}: installed native/WASM parity, controls, replay, ${puzzle.realizations.length} views`);
    } finally {controller.dispose();session.dispose();}
  }
  // The optional authoring module is tested separately from the isolated consumer.
  const {createAuthoring,migrateSourceSession}=await import(pathToFileURL(join(root,'build/kernel/authoring.js')));
  const full=await createAuthoring();const source=await read(join(root,'packages/cube3/package.json'));
  const old=new full.Session(source);old.lightweightResults(true);
  try {
    old.runJSON('R U','transactional','0');old.undoJSON(old.revision());
    const oldSaveText=old.saveWithPresentationJSON('{"viewOrder":["diagram","cube"],"visibleViews":["cube"],"counter":9223372036854775807}');
    const oldSave=JSON.parse(oldSaveText);
    const convertedText=migrateSourceSession(full,source,oldSaveText);
    assert.match(convertedText,/"counter":9223372036854775807/,'Migration rounded an opaque presentation integer');
    const converted=JSON.parse(convertedText);
    assert.equal(converted.schemaVersion,3);assert.equal(converted.logical.cursor,1);assert.equal(converted.logical.requests.length,2);
    assert.deepEqual(converted.presentation,oldSave.presentation);
    const readCube=name=>read(join(root,'runtime-puzzles/cube3',name+'.json'));
    const session=new RuntimeSession(module,{abstract:await readCube('abstract'),controls:await readCube('controls'),presentation:await readCube('presentation')});
    try {assert.equal(session.load(JSON.stringify(converted)).status,'Loaded');assert.equal(session.snapshot().canRedo,true);assert.equal(session.redo().status,'Committed');assert.equal(session.undo().status,'Committed');}
    finally {session.dispose();}
    const bad=structuredClone(oldSave);bad.logical.definitionDigest='0'.repeat(64);
    assert.throws(()=>migrateSourceSession(full,source,JSON.stringify(bad)));
    const forged=structuredClone(oldSave);forged.logical.requests[0].afterStateDigest='0'.repeat(64);
    assert.throws(()=>migrateSourceSession(full,source,JSON.stringify(forged)));
  } finally {old.delete();}
  await mkdir(join(root,'build/runtime-check'),{recursive:true});
  await writeFile(join(root,'build/runtime-check/measurements.json'),JSON.stringify({initializationMs,wasmBytes:(await stat(join(javascript,'generated/twisty-artifact.wasm'))).size,puzzles:measurements,nodePeakRSSKiB:process.resourceUsage().maxRSS},null,2)+'\n');
  assert.equal(views,48);console.log(`Compiler-free distribution: ${inventory.puzzles.length} puzzles, ${views} realizations, negative contracts and verified schema-2 conversion passed.`);
} finally {await rm(temporary,{recursive:true,force:true});}
