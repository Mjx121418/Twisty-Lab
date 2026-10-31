// Explicit authoring step; artifact-only consumers never execute the producer.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
const args = process.argv.slice(2);
assert.ok(args.every(a => a === '--write'), 'Usage: build-artifact-examples.mjs [--write]');
for (const [name, flag] of [['cube3', '--cube-artifact'], ['bandaged', '--bandaged-artifact']]) {
  const bundle = JSON.parse(execFileSync('build/native/twisty_language', [flag], { cwd: root, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024 }));
  const dir = new URL(`../examples/artifact/data/${name}/`, import.meta.url);
  if (args.includes('--write')) await mkdir(dir, { recursive: true });
  for (const [kind, value] of Object.entries(bundle)) {
    const path = new URL(`${kind}.json`, dir), text = JSON.stringify(value, null, 2) + '\n';
    if (args.includes('--write')) await writeFile(path, text);
    else assert.equal(await readFile(path, 'utf8'), text, `${name}/${kind} differs from its producer`);
  }
  console.log(`${name}: source-free abstract, interpretation and explicit cube tracks ${args.includes('--write') ? 'written' : 'reproduced'}.`);
}
