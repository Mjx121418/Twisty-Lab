// Each geometry case runs sequentially in a fresh process under the 4 GiB budget.
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {cp,readFile,writeFile,mkdir,mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import Ajv from 'ajv';
const root=fileURLToPath(new URL('../',import.meta.url)),published=join(root,'build/kernel');
const run=(file,args)=>{try{return execFileSync(file,args,{cwd:root,encoding:'utf8',maxBuffer:32*1024*1024});}
 catch(error){throw new Error(`${file} failed: ${String(error.stderr??error.message).slice(-6000)}`);}};
const temporary=await mkdtemp(join(tmpdir(),'twisty-interpretation-'));
const inventory=JSON.parse(await readFile(join(root,'scripts/puzzle-inventory.json'),'utf8'));
const selected=process.argv.slice(2);
const baseline=JSON.parse(await readFile(join(root,'tests/fixtures/architecture-baseline.json'),'utf8'));
function near(a,b,path=''){
 if(typeof a==='number'&&typeof b==='number')assert.ok(Number.isFinite(a)&&Math.abs(a-b)<=1e-8*(1+Math.abs(b)),path);
 else if(Array.isArray(a)&&Array.isArray(b)){assert.equal(a.length,b.length,path);a.forEach((x,i)=>near(x,b[i],`${path}/${i}`));}
 else if(a&&b&&typeof a==='object'&&typeof b==='object'){assert.deepEqual(Object.keys(a).sort(),Object.keys(b).sort(),path);Object.keys(a).forEach(k=>near(a[k],b[k],`${path}/${k}`));}
 else assert.deepEqual(a,b,path);
}
try{
 run('cmake',['--build','build/native','--parallel','1','--target','twisty_artifact_export','artifact_consumer']);
 const installed=join(temporary,'installed');run('cmake',['--install','build/native','--prefix',installed]);
 const consumer=join(temporary,'consumer');await cp(join(published,'examples/artifact'),consumer,{recursive:true});
 run('cmake',['-S',consumer,'-B',join(consumer,'build'),'-G','Ninja',`-DCMAKE_PREFIX_PATH=${installed}`]);run('cmake',['--build',join(consumer,'build'),'--parallel','1']);
 assert.ok(!/libpuzzle_(?:language|execution|session|geometry|authoring_geometry)\.a/.test(run('ninja',['-C',join(consumer,'build'),'-t','commands'])));
 const authoring=join(consumer,'authoring-build');
 run('cmake',['-S',consumer,'-B',authoring,'-G','Ninja',`-DCMAKE_PREFIX_PATH=${installed}`,'-DTWISTY_BUILD_ARTIFACT_EXPORTER=ON']);
 run('cmake',['--build',authoring,'--parallel','1','--target','artifact_export']);
 const exporter=join(authoring,'artifact_export');
 const isolated=join(temporary,'kernel');await mkdir(join(isolated,'generated'),{recursive:true});
 for(const file of ['artifact.js','artifact.d.ts'])await cp(join(published,file),join(isolated,file));
 for(const file of ['twisty-artifact.mjs','twisty-artifact.wasm','twisty-artifact.d.mts'])await cp(join(published,'generated',file),join(isolated,'generated',file));
 const reference=join(temporary,'reference');await mkdir(reference);
 for(const file of ['twisty.mjs','twisty.wasm'])await cp(join(published,'generated',file),join(reference,file));
 const probe=join(temporary,'probe.mjs');await cp(join(root,'tests/interpretation_wasm.mjs'),probe);
 const ajv=new Ajv({strict:false,allErrors:true});
 for(const name of ['artifact-common','compact-v2','abstract-artifact','interpretation-v2','realization-envelope','artifact-backend','backend-payload'])ajv.addSchema(JSON.parse(await readFile(join(published,`schemas/${name}.schema.json`),'utf8')));
 const valid=(id,value)=>{const validator=ajv.getSchema(`urn:twisty:schema:${id}`);assert.ok(validator);assert.ok(validator(value),ajv.errorsText(validator.errors));};
 let count=0;
 for(const puzzle of inventory.puzzles)for(const file of puzzle.realizations){
  if(selected.length && !selected.some(value=>value===puzzle.id || value===`${puzzle.id}/${file}`))continue;
  const original=join(root,'packages',puzzle.id),source=join(original,'package.json');
  const bundleText=run(exporter,[source,join(original,file)]),bundle=JSON.parse(bundleText);
  valid('abstract-artifact:1',bundle.abstract);valid('interpretation:2',bundle.interpretation);valid('realization-envelope:1',bundle.realization);valid('backend-payload:1',JSON.parse(bundle.payload));
  // Re-export representative difficult families to verify deterministic bytes.
  if(count%7===0 && !puzzle.id.startsWith('cell24'))assert.equal(run(exporter,[source,join(original,file)]),bundleText);
  const directory=join(temporary,'case');await rm(directory,{recursive:true,force:true});await mkdir(directory);
  for(const key of ['abstract','interpretation','realization'])await writeFile(join(directory,`${key}.json`),JSON.stringify(bundle[key]));
  await writeFile(join(directory,'geometry.json'),bundle.payload);
  await writeFile(join(directory,'case.json'),JSON.stringify({algorithm:baseline.cases.find(c=>c.id===puzzle.id)?.algorithm}));
  await cp(source,join(directory,'source-package.json'));await cp(join(original,file),join(directory,'source-realization.json'));
  run(process.execPath,['--max-old-space-size=384',probe,directory,join(isolated,'artifact.js'),join(reference,'twisty.mjs')]);
  const wasm=JSON.parse(await readFile(join(directory,'wasm-report.json'),'utf8'));
  if(wasm.aliasAlgorithm){
   const alias=JSON.parse(run(exporter,[join(directory,'alias-source.json'),join(directory,'alias-realization.json')]));
   valid('abstract-artifact:1',alias.abstract);valid('interpretation:2',alias.interpretation);valid('realization-envelope:1',alias.realization);
  }
  const native=JSON.parse(run(join(consumer,'build/artifact_consumer'),['abstract.json','interpretation.json','realization.json','geometry.json'].map(f=>join(directory,f)).concat(wasm.algorithm)));
  const {frames,...logical}=wasm.report,{frames:expected,...nativeLogical}=native;assert.deepEqual(logical,nativeLogical);near(frames,expected,'native/WASM poses');
  console.log(`${++count}/48 ${puzzle.id}/${file}: compiler-free native/WASM replay, witnesses, assets, poses and opaque IDs agree.`);
 }
 assert.equal(count,inventory.puzzles.reduce((n,p)=>n+p.realizations.filter(file=>!selected.length||selected.some(value=>value===p.id||value===`${p.id}/${file}`)).length,0));assert.ok(count>0,'No matching interpretation cases.');
}finally{await rm(temporary,{recursive:true,force:true});}
