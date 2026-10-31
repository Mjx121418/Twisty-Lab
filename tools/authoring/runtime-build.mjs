// Explicit offline export, separate from normal web and SDK builds.
import { execFileSync } from 'node:child_process';
import { cp, mkdir, mkdtemp, open, readFile, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import { root, readInventory, checkFreshness, hashFiles } from '../../scripts/puzzle-data.mjs';
import { runtimeInputs, runtimeFiles, runtimeRoot } from './runtime-data.mjs';
await mkdir(runtimeRoot,{recursive:true});
const lock=await open(join(runtimeRoot,'.runtime-build.lock'),'wx');
let stage;
try {
  await lock.writeFile(JSON.stringify({pid:process.pid})+'\n');
  const inventory=await readInventory();
  await checkFreshness(root,inventory);
  const before=await runtimeInputs(inventory);
  stage=await mkdtemp(join(root,'build/.runtime-stage-'));
  for (const puzzle of inventory.puzzles) {
    const output=join(stage,puzzle.id),previous=join(runtimeRoot,puzzle.id);
    try {
      await readFile(join(previous,'build-manifest.json'));
      await cp(previous,output,{recursive:true});
    } catch(error) { if(error.code!=='ENOENT') throw error; }
    execFileSync('python3',['tools/authoring/twisty-author','build',`tools/authoring/runtime-projects/${puzzle.id}.json`,'--output',output,'--tools-dir','build/native'],{cwd:root,stdio:'inherit'});
  }
  const inputs=await runtimeInputs(inventory);
  if (!isDeepStrictEqual(before,inputs)) throw new Error('Inputs changed while exporting runtime data.');
  const manifest={formatVersion:1,inputs,artifacts:await hashFiles(stage,runtimeFiles(inventory))};
  await writeFile(join(stage,'build-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
  const publisher=`from pathlib import Path
import sys
from tools.authoring.transaction import publish
stage=Path(sys.argv[1]); destination=Path(sys.argv[2])
entries=[(stage/name,destination/name) for name in sys.argv[3:]]
entries.append((stage/'build-manifest.json',destination/'build-manifest.json'))
publish(entries,destination.parent/'build')`;
  execFileSync('python3',['-B','-c',publisher,stage,runtimeRoot,...inventory.puzzles.map(p=>p.id)],{cwd:root,stdio:'inherit'});
  console.log(`Exported and validated ${inventory.puzzles.length} runtime puzzles.`);
} finally {
  if(stage) await rm(stage,{recursive:true,force:true});
  await lock.close();await rm(join(runtimeRoot,'.runtime-build.lock'),{force:true});
}
