import { execFileSync } from 'node:child_process';
import { mkdir, mkdtemp, open, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import { artifactPaths, hashFiles, inputHashes, manifestPath, readInventory, root, validatePuzzleData } from '../../scripts/puzzle-data.mjs';

let stage, lock;
const lockPath = join(root, 'packages/.authoring-build.lock');
try {
  const args = process.argv.slice(2);
  if (args.some((arg) => arg !== '--update-fixtures')) throw new Error('Usage: npm run build:puzzles [-- --update-fixtures]');
  lock = await open(lockPath, 'wx');
  const inventory = await readInventory();
  const run = (command, args) => execFileSync(command, args, { cwd: root, stdio: 'inherit', env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' } });
  run('cmake', ['--preset', 'native']);
  run('cmake', ['--build', '--preset', 'native', '--target', 'twisty', 'twisty_language', 'melty_solids', 'twisty_artifact_export', 'twisty_artifact_validate']);
  const before = await inputHashes(root, inventory);
  await mkdir(join(root, 'build'), { recursive: true });
  stage = await mkdtemp(join(root, 'build/.puzzle-stage-'));
  const common = ['--workspace', root, '--stage', stage, '--tools-dir', join(root, 'build/native'), ...args];
  run('python3', ['-B', '-m', 'tools.authoring.legacy', 'stage', ...common]);
  const result = await validatePuzzleData(stage, inventory, (path) => {
    const compiled = JSON.parse(execFileSync(join(root, 'build/native/twisty'), ['validate', '--definition', path, '--json'], { cwd: stage, encoding: 'utf8' }));
    if (compiled.status !== 'Valid') throw new Error(JSON.stringify(compiled));
    return { digest: compiled.definitionDigest };
  });
  if (!isDeepStrictEqual(before, await inputHashes(root, inventory))) throw new Error('Authoring inputs changed during generation; rerun npm run build:puzzles.');
  const artifacts = await hashFiles(stage, artifactPaths(inventory));
  // A normalized seed can also be an output. Record its published bytes.
  const inputs = { ...before };
  for (const path of Object.keys(inputs)) if (path in artifacts) inputs[path] = artifacts[path];
  await writeFile(join(stage, manifestPath), JSON.stringify({ formatVersion: 1, inputs, artifacts }, null, 2) + '\n');
  run('python3', ['-B', '-m', 'tools.authoring.legacy', 'publish', ...common]);
  console.log(`Built and validated ${result.puzzles} puzzle packages and ${result.realizations} realizations; recorded ${manifestPath}.`);
} catch (error) {
  console.error(error.message ?? error);
  process.exitCode = 1;
} finally {
  if (stage) await rm(stage, { recursive: true, force: true });
  if (lock) { await lock.close(); await rm(lockPath, { force: true }); }
}
