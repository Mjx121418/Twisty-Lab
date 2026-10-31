// Repository runtime-data provenance. Consumers need only the emitted documents.
import { readdir, readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import { artifactPaths, hashFiles, inputHashes, readInventory, root } from '../../scripts/puzzle-data.mjs';
export const runtimeRoot = join(root, 'runtime-puzzles');
export async function runtimeInputs(inventory) {
  return { ...await inputHashes(root, inventory), ...await hashFiles(root, artifactPaths(inventory)) };
}
export function runtimeFiles(inventory) {
  return inventory.puzzles.flatMap(puzzle => [
    ...['abstract','controls','presentation','interpretation','build-manifest'].map(name => `${puzzle.id}/${name}.json`),
    ...puzzle.realizations.flatMap(name => ['realization','geometry','presentation'].map(part => `${puzzle.id}/realizations/${name.slice(0,-5)}/${part}.json`)),
  ]).sort();
}
export async function runtimeManifest(inventory) {
  inventory ??= await readInventory();
  return { formatVersion: 1, inputs: await runtimeInputs(inventory), artifacts: await hashFiles(runtimeRoot, runtimeFiles(inventory)) };
}
export async function checkRuntimeData() {
  const inventory = await readInventory();
  const recorded = JSON.parse(await readFile(join(runtimeRoot,'build-manifest.json'),'utf8'));
  const actual = await runtimeManifest(inventory);
  if (!isDeepStrictEqual(recorded,actual)) throw new Error('Runtime data is stale or modified; run npm run build:runtime-puzzles.');
  const expected = new Set([...runtimeFiles(inventory),'build-manifest.json']);
  async function visit(relative='') {
    for (const item of await readdir(join(runtimeRoot,relative),{withFileTypes:true})) {
      if (item.name.startsWith('.')) continue;
      const path = relative ? `${relative}/${item.name}` : item.name;
      if (item.isSymbolicLink()) throw new Error(`Runtime data contains a symlink: ${path}`);
      if (item.isDirectory()) await visit(path);
      else if (!expected.has(path)) throw new Error(`Unlisted runtime data: ${path}`);
    }
  }
  await visit();
  return { puzzles:inventory.puzzles.length, realizations:inventory.puzzles.reduce((n,p)=>n+p.realizations.length,0) };
}
