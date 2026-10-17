import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
if (process.argv.includes('--stage-web')) {
  await rm(new URL('../dist/kernel/', import.meta.url), { recursive: true, force: true });
  await cp(new URL('../build/kernel/', import.meta.url), new URL('../dist/kernel/', import.meta.url), { recursive: true });
} else {
  const output = new URL('../build/kernel/', import.meta.url);
  await rm(output, { recursive: true, force: true });
  await mkdir(output, { recursive: true });
  execFileSync('node_modules/.bin/tsc', ['-p', 'tsconfig.kernel.json'], { cwd: root, stdio: 'inherit' });
  await cp(new URL('../kernel/generated/', import.meta.url), new URL('generated/', output), { recursive: true });
  const { version } = JSON.parse(await readFile(new URL('../package.json', import.meta.url), 'utf8'));
  await writeFile(new URL('package.json', output), JSON.stringify({
    name: '@twisty/kernel', version, type: 'module',
    exports: { '.': { types: './index.d.ts', import: './index.js' }, './runtime': './generated/twisty.mjs', './wasm': './generated/twisty.wasm' },
    files: ['index.js', 'index.d.ts', 'generated', 'examples', 'schemas', 'README.md', 'relational-core.md', 'spherical-cube.md', 'spherical-helicopter.md', 'spherical-bagua.md', 'bagua-model.md', 'bagua-verification.md', 'gear-model.md', 'gear-spherical-feasibility.md'],
  }, null, 2) + '\n');
  const readme = (await readFile(new URL('../docs/kernel-api.md', import.meta.url), 'utf8'))
    .replaceAll('../schemas/', 'schemas/').replaceAll('../examples/', 'examples/');
  await writeFile(new URL('README.md', output), readme);
  for (const file of ['relational-core.md', 'spherical-cube.md', 'spherical-helicopter.md', 'spherical-bagua.md', 'bagua-model.md', 'bagua-verification.md', 'gear-model.md', 'gear-spherical-feasibility.md']) {
    const guide = (await readFile(new URL(`../docs/${file}`, import.meta.url), 'utf8'))
      .replaceAll('../packages/', 'examples/canvas/data/').replaceAll('../examples/', 'examples/')
      .replaceAll('../schemas/', 'schemas/').replaceAll('[migration plan](abstract-language-core-plan.md)', 'migration plan')
      .replaceAll('(kernel-api.md', '(README.md');
    await writeFile(new URL(file, output), guide);
  }
  await cp(new URL('../examples/canvas/', import.meta.url), new URL('examples/canvas/', output), { recursive: true });
  await cp(new URL('../examples/relational/', import.meta.url), new URL('examples/relational/', output), { recursive: true });
  await mkdir(new URL('schemas/', output));
  await cp(new URL('../schemas/relational-puzzle.schema.json', import.meta.url), new URL('schemas/relational-puzzle.schema.json', output));
  for (const file of ['relational-package.schema.json', 'relational-gear-source.schema.json', 'session-v2.schema.json', 'definition.schema.json'])
    await cp(new URL(`../schemas/${file}`, import.meta.url), new URL(`schemas/${file}`, output));
  for (const puzzle of ['cube3', 'bandaged', 'helicopter', 'bagua', 'gear', 'gear-extreme', 'gear-plain']) {
    const directory = new URL(`examples/canvas/data/${puzzle}/`, output);
    await mkdir(directory, { recursive: true });
    const prefix = puzzle === 'helicopter' || puzzle === 'bagua' || puzzle.startsWith('gear') ? puzzle : 'cube';
    const files = ['definition.json', `${prefix}-port-diagram.json`];
    if (puzzle === 'cube3' || puzzle === 'bandaged') files.push('relational.pzl.json', 'relational-bindings.json',
      'relational-package.json', 'relational-cube-euclidean.json', 'relational-cube-port-diagram.json', 'relational-cube-spherical.json');
    if (puzzle === 'helicopter') files.push('relational.pzl.json', 'relational-bindings.json',
      'relational-package.json', 'relational-helicopter-euclidean.json', 'relational-helicopter-port-diagram.json', 'relational-helicopter-spherical.json');
    if (puzzle === 'bagua') files.push('review.json', 'bagua-spherical.json', 'relational.pzl.json', 'relational-bindings.json',
      'relational-package.json', 'relational-bagua-euclidean.json', 'relational-bagua-port-diagram.json', 'relational-bagua-spherical.json');
    files.push(`${prefix}-${puzzle === 'bagua' || puzzle.startsWith('gear') ? 'euclidean' : 'spherical'}.json`);
    if (puzzle.startsWith('gear')) files.push('source.json', `${prefix}-six-teeth-euclidean.json`, `${prefix}-six-teeth-port-diagram.json`);
    if (puzzle === 'gear') files.push('gear-normal-euclidean.json', 'normal-meshes.json', 'spherical-meshes.json');
    if (puzzle.startsWith('gear')) files.push(`${prefix}-supplied-euclidean.json`, `${prefix}-spherical.json`);
    if (puzzle === 'gear-extreme') files.push('plain-edge-meshes.json');
    if (puzzle.startsWith('gear')) {
      files.push('relational-source.json', 'relational.pzl.json', 'relational-bindings.json', 'relational-package.json', 'relational-manifest.json');
      files.push(...files.filter((file) => file.startsWith(puzzle + '-') && file.endsWith('.json')).map((file) => 'relational-' + file));
    }
    for (const file of files) {
      await cp(new URL(`../packages/${puzzle}/${file}`, import.meta.url), new URL(file, directory));
    }
  }
  console.log(`Portable kernel ${version}: build/kernel`);
}
