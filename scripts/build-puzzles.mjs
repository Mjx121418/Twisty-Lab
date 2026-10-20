import { execFileSync } from 'node:child_process';
import { rename, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import { inputHashes, makeManifest, manifestPath, readInventory, root, validatePuzzleData } from './puzzle-data.mjs';

const temporary = join(root, `${manifestPath}.tmp`);
try {
  const args = process.argv.slice(2);
  if (args.some((arg) => arg !== '--update-fixtures')) throw new Error('Usage: npm run build:puzzles [-- --update-fixtures]');
  const inventory = await readInventory();
  const before = await inputHashes(root, inventory);
  const run = (command, args) => execFileSync(command, args, { cwd: root, stdio: 'inherit' });
  run('cmake', ['--preset', 'native']);
  run('cmake', ['--build', '--preset', 'native', '--target', 'twisty', 'twisty_language']);
  run('python3', ['scripts/compile-packages.py', '--write', ...args]);
  const result = await validatePuzzleData(root, inventory, (path) => {
    const compiled = JSON.parse(execFileSync(join(root, 'build/native/twisty'), ['validate', '--definition', path, '--json'], { cwd: root, encoding: 'utf8' }));
    if (compiled.status !== 'Valid') throw new Error(JSON.stringify(compiled));
    return { digest: compiled.definitionDigest };
  });
  const manifest = await makeManifest(root, inventory);
  if (!isDeepStrictEqual(before, manifest.inputs)) throw new Error('Authoring inputs changed during generation; rerun npm run build:puzzles.');
  await writeFile(temporary, JSON.stringify(manifest, null, 2) + '\n');
  await rename(temporary, join(root, manifestPath));
  console.log(`Built ${result.puzzles} puzzle packages and recorded ${manifestPath}.`);
} catch (error) {
  console.error(error.message ?? error);
  process.exitCode = 1;
} finally { await rm(temporary, { force: true }); }
