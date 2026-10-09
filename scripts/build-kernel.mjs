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
    files: ['index.js', 'index.d.ts', 'generated', 'examples', 'README.md', 'spherical-cube.md', 'spherical-helicopter.md', 'bagua-model.md'],
  }, null, 2) + '\n');
  await cp(new URL('../docs/kernel-api.md', import.meta.url), new URL('README.md', output));
  for (const file of ['spherical-cube.md', 'spherical-helicopter.md', 'bagua-model.md']) {
    const guide = (await readFile(new URL(`../docs/${file}`, import.meta.url), 'utf8'))
      .replaceAll('../packages/', 'examples/canvas/data/').replaceAll('(kernel-api.md)', '(README.md)');
    await writeFile(new URL(file, output), guide);
  }
  await cp(new URL('../examples/canvas/', import.meta.url), new URL('examples/canvas/', output), { recursive: true });
  for (const puzzle of ['cube3', 'bandaged', 'helicopter', 'bagua']) {
    const directory = new URL(`examples/canvas/data/${puzzle}/`, output);
    await mkdir(directory, { recursive: true });
    const prefix = puzzle === 'helicopter' || puzzle === 'bagua' ? puzzle : 'cube';
    const files = ['definition.json', `${prefix}-port-diagram.json`];
    files.push(`${prefix}-${puzzle === 'bagua' ? 'euclidean' : 'spherical'}.json`);
    for (const file of files) {
      await cp(new URL(`../packages/${puzzle}/${file}`, import.meta.url), new URL(file, directory));
    }
  }
  console.log(`Portable kernel ${version}: build/kernel`);
}
