import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const native = resolve(root, process.argv[2] ?? 'build/native/twisty_relational');
const sdk = resolve(root, process.argv[3] ?? 'build/kernel/index.js');
const temporary = await mkdtemp(join(tmpdir(), 'twisty-relational-'));
try {
  const oracle = join(temporary, 'oracle.json');
  execFileSync('python3', [join(root, 'tests/relational_oracle.py'), native,
    join(root, 'examples/relational/coupled.pzl.json'), '--emit', oracle], { stdio: 'inherit' });
  const { createKernel, ExperimentalRelationalPuzzle } = await import(pathToFileURL(sdk).href);
  const module = await createKernel();
  assert.equal(JSON.parse(module.kernelInfoJSON()).experimentalAPIs.relational, 1);
  assert.equal(JSON.parse(module.compileRelationalJSON('{}')).status, 'Invalid');
  let count = 0;
  for (const suite of JSON.parse(await readFile(oracle, 'utf8'))) {
    const puzzle = new ExperimentalRelationalPuzzle(module, suite.source);
    try {
      assert.deepEqual(puzzle.preview, suite.preview, 'Native/WASM compiler output differs.');
      for (let i = 0; i < suite.queries.length; ++i) {
        assert.deepEqual(puzzle.query(suite.queries[i]), suite.expected[i], JSON.stringify(suite.queries[i]));
        ++count;
      }
    } finally { puzzle.dispose(); }
  }
  const source = await readFile(join(root, 'examples/relational/coupled.pzl.json'), 'utf8');
  const puzzle = new ExperimentalRelationalPuzzle(module, source);
  try {
    assert.equal(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'synchronize' }).status, 'ChoiceRequired');
    assert.deepEqual(puzzle.query({ op: 'bind', state: [0, 2, 4], request: 'fixed' }),
      { status: 'Selected', choices: ['turn', 'turn', 'idle'] });
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 1, resultLimit: 10 }).complete, false);
    assert.equal(puzzle.query({ op: 'discover', state: [0, 2, 4], request: 'none', candidateLimit: 100, resultLimit: 10 }).complete, true);
    assert.equal(puzzle.query({ op: 'plan', state: [0, 2, 4], request: 'fixed', choices: [] }).status, 'Invalid');
  } finally { puzzle.dispose(); }
  console.log(`Portable relational API: ${count} WASM results match the independent source oracle and native consumer.`);
} finally { await rm(temporary, { recursive: true, force: true }); }
