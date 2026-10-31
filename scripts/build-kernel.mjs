import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { checkFreshness, puzzleFiles, readInventory } from './puzzle-data.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
if (process.argv.includes('--stage-web')) {
  await rm(new URL('../dist/kernel/', import.meta.url), { recursive: true, force: true });
  await cp(new URL('../build/kernel/', import.meta.url), new URL('../dist/kernel/', import.meta.url), { recursive: true });
} else {
  const inventory = await readInventory(root);
  await checkFreshness(root, inventory);
  const output = new URL('../build/kernel/', import.meta.url);
  await rm(output, { recursive: true, force: true });
  await mkdir(output, { recursive: true });
  execFileSync('node_modules/.bin/tsc', ['-p', 'tsconfig.kernel.json'], { cwd: root, stdio: 'inherit' });
  await cp(new URL('../kernel/generated/', import.meta.url), new URL('generated/', output), { recursive: true });
  const { version } = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8'));
  await writeFile(new URL('package.json', output), JSON.stringify({
    name: '@twisty/kernel', version, type: 'module',
    exports: { '.': { types: './index.d.ts', import: './index.js' }, './artifact': { types: './artifact.d.ts', import: './artifact.js' }, './execution': { types: './runtime.d.ts', import: './runtime.js' }, './authoring': { types: './authoring.d.ts', import: './authoring.js' }, './runtime': './generated/twisty.mjs', './wasm': './generated/twisty.wasm', './artifact-wasm': './generated/twisty-artifact.wasm' },
    files: ['runtime.js', 'runtime.d.ts', 'shared.js', 'shared.d.ts', 'authoring.js', 'authoring.d.ts', 'index.js', 'index.d.ts', 'artifact.js', 'artifact.d.ts', 'generated', 'examples', 'schemas', 'README.md', 'core-language.md', 'reference-anchors.md', 'spherical-cube.md', 'spherical-helicopter.md', 'spherical-bagua.md', 'bagua-model.md', 'gear-model.md', 'icosahedron-model.md', 'flat-surfaces.md', 'authoring-tool.md', 'runtime-sdk.md'],
  }, null, 2) + '\n');
  const readme = (await readFile(new URL('../docs/kernel-api.md', import.meta.url), 'utf8'))
    .replaceAll('../schemas/', 'schemas/').replaceAll('../examples/', 'examples/');
  await writeFile(new URL('README.md', output), readme);
  for (const file of ['core-language.md', 'reference-anchors.md', 'spherical-cube.md', 'spherical-helicopter.md', 'spherical-bagua.md', 'bagua-model.md', 'gear-model.md', 'icosahedron-model.md', 'flat-surfaces.md', 'authoring-tool.md', 'runtime-sdk.md']) {
    const guide = (await readFile(new URL(`../docs/${file}`, import.meta.url), 'utf8'))
      .replaceAll('../packages/', 'examples/canvas/data/').replaceAll('../examples/', 'examples/')
      .replaceAll('../schemas/', 'schemas/').replaceAll('[language roadmap](abstract-language-core-plan.md)', 'language roadmap')
      .replaceAll('(kernel-api.md', '(README.md');
    await writeFile(new URL(file, output), guide);
  }
  await cp(new URL('../examples/canvas/', import.meta.url), new URL('examples/canvas/', output), { recursive: true });
  await cp(new URL('../examples/language/', import.meta.url), new URL('examples/language/', output), { recursive: true });
  await cp(new URL('../examples/runtime/', import.meta.url), new URL('examples/runtime/', output), { recursive: true });
  await cp(new URL('../examples/artifact/', import.meta.url), new URL('examples/artifact/', output), { recursive: true });
  await cp(new URL('../examples/runtime-native/', import.meta.url), new URL('examples/runtime-native/', output), { recursive: true });
  await mkdir(new URL('schemas/', output));
  await cp(new URL('../schemas/puzzle.schema.json', import.meta.url), new URL('schemas/puzzle.schema.json', output));
  for (const file of ['puzzle-v2.schema.json', 'puzzle-v3.schema.json', 'compact.schema.json', 'compact-v2.schema.json', 'package.schema.json', 'gear-source.schema.json', 'session-v2.schema.json', 'state.schema.json', 'realization.schema.json', 'abstract-artifact.schema.json', 'artifact-common.schema.json', 'interpretation.schema.json', 'artifact-realization.schema.json', 'artifact-witness.schema.json', 'session-v3.schema.json', 'interpretation-v2.schema.json', 'realization-envelope.schema.json', 'backend-payload.schema.json', 'artifact-backend.schema.json', 'control-artifact.schema.json'])
    await cp(new URL(`../schemas/${file}`, import.meta.url), new URL(`schemas/${file}`, output));
  for (const puzzle of inventory.puzzles) {
    const directory = new URL(`examples/canvas/data/${puzzle.id}/`, output);
    await mkdir(directory, { recursive: true });
    for (const file of puzzleFiles(puzzle))
      await cp(new URL(`../packages/${puzzle.id}/${file}`, import.meta.url), new URL(file, directory));
  }
  console.log(`Portable kernel ${version}: build/kernel`);
}
