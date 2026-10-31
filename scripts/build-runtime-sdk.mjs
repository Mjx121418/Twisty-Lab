// A distinct distribution with no source compiler, factories or source fixtures.
import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { checkRuntimeData } from '../tools/authoring/runtime-data.mjs';
import { readInventory } from './puzzle-data.mjs';
const root=fileURLToPath(new URL('../',import.meta.url));
const args=process.argv.slice(2);
if(args.some(arg=>!['--with-presets','--stage-web'].includes(arg)))throw new Error('Usage: build-runtime-sdk.mjs [--with-presets] [--stage-web]');
if(args.includes('--stage-web')) {
  await rm(join(root,'dist/runtime'),{recursive:true,force:true});
  await cp(join(root,'build/runtime-sdk'),join(root,'dist/runtime'),{recursive:true});
} else {
  const output=join(root,'build/runtime-sdk');
  execFileSync('node_modules/.bin/tsc',['-p','tsconfig.runtime.json'],{cwd:root,stdio:'inherit'});
  await rm(output,{recursive:true,force:true});await mkdir(join(output,'generated'),{recursive:true});
  for(const name of ['runtime.js','runtime.d.ts','artifact.js','artifact.d.ts','shared.js','shared.d.ts'])
    await cp(join(root,'build/runtime-code',name),join(output,name));
  for(const name of ['twisty-artifact.mjs','twisty-artifact.wasm','twisty-artifact.d.mts'])
    await cp(join(root,'kernel/generated',name),join(output,'generated',name));
  const {version}=JSON.parse(await readFile(join(root,'package.json'),'utf8'));
  await writeFile(join(output,'package.json'),JSON.stringify({name:'@twisty/runtime',version,type:'module',
    exports:{'.':{types:'./runtime.d.ts',import:'./runtime.js'},'./artifact':{types:'./artifact.d.ts',import:'./artifact.js'},'./raw':'./generated/twisty-artifact.mjs','./wasm':'./generated/twisty-artifact.wasm'},
    files:['runtime.js','runtime.d.ts','shared.js','shared.d.ts','artifact.js','artifact.d.ts','generated','examples','schemas','README.md','kernel-api.md',...(args.includes('--with-presets')?['data']:[])]},null,2)+'\n');
  await writeFile(join(output,'README.md'),(await readFile(join(root,'docs/runtime-sdk.md'),'utf8')).replaceAll('docs/kernel-api.md','kernel-api.md'));
  await cp(join(root,'docs/kernel-api.md'),join(output,'kernel-api.md'));
  await mkdir(join(output,'schemas'));
  for(const name of ['abstract-artifact','artifact-common','compact-v2','interpretation-v2','realization-envelope','backend-payload','artifact-backend','control-artifact','presentation-artifact','artifact-witness','session-v3'])
    await cp(join(root,'schemas',name+'.schema.json'),join(output,'schemas',name+'.schema.json'));
  await cp(join(root,'examples/runtime-canvas'),join(output,'examples/canvas'),{recursive:true});
  if(args.includes('--with-presets')) {
    await checkRuntimeData();const inventory=await readInventory();
    await cp(join(root,'runtime-puzzles'),join(output,'data'),{recursive:true,filter:path=>!path.split('/').at(-1).startsWith('.')});
    await writeFile(join(output,'data/index.json'),JSON.stringify({puzzles:inventory.puzzles.map(p=>({id:p.id,views:p.realizations.map(name=>name.slice(0,-5))}))},null,2)+'\n');
  }
  console.log(`Compiler-free runtime SDK: ${output}${args.includes('--with-presets')?' (with optional presets)':''}`);
}
