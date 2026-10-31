// Capture or check the Phase-1 baseline. Heavy work is deliberately sequential.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { checkDraftContracts } from '../tests/architecture/contracts.mjs';
import { frozenKernelDiscovery } from '../tests/architecture/backend-discovery.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const args = process.argv.slice(2);
assert.ok(args.every(arg => arg === '--record'), 'Usage: node scripts/check-architecture.mjs [--record]');
const record = args.includes('--record');
const fixturePath = join(root, 'tests/fixtures/architecture-baseline.json');
const baseline = JSON.parse(await readFile(fixturePath, 'utf8'));
assert.equal(baseline.schemaVersion, 1);
assert.equal(baseline.kind, 'architecture-baseline');
assert.equal(baseline.cases.length, 7);
assert.equal(new Set(baseline.cases.map(c => c.id)).size, baseline.cases.length);
const run = (file, args, options = {}) => execFileSync(file, args, { cwd: root, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, ...options });
const build = join(root, 'build/architecture');
const installed = join(build, 'installed');
const consumer = join(build, 'consumer');
await mkdir(build, { recursive: true });
console.log(`${await checkDraftContracts()} draft structure/hash conformance checks passed.`);
// Leave enough room for application/tools under the workspace's 4 GiB budget.
async function checkMemory() {
  try {
    const used = Number(await readFile('/sys/fs/cgroup/memory.current', 'utf8'));
    const rawLimit = (await readFile('/sys/fs/cgroup/memory.max', 'utf8')).trim();
    if (rawLimit !== 'max') assert.ok(Number(rawLimit) - used >= 1024 * 1024 * 1024,
      'Less than 1 GiB cgroup headroom: stop other heavy work before checking architecture.');
  } catch (error) { if (error.code !== 'ENOENT') throw error; }
}
await checkMemory();
run('cmake', ['--build', 'build/native', '--parallel', '1', '--target', 'puzzle_controller', 'puzzle_geometry', 'puzzle_language'], { stdio: 'inherit' });
run('cmake', ['--install', 'build/native', '--prefix', installed]);
run('cmake', ['-S', 'tests/architecture', '-B', consumer, '-G', 'Ninja', `-DCMAKE_PREFIX_PATH=${installed}`]);
run('cmake', ['--build', consumer, '--parallel', '1'], { stdio: 'inherit' });

function compare(actual, expected, context, discoveryExtension = false) {
  const { views, ...exact } = actual;
  const { views: expectedViews, ...expectedExact } = expected;
  if (discoveryExtension && !expectedExact.kernel.geometryBackends) exact.kernel = frozenKernelDiscovery(exact.kernel, expectedExact.kernel);
  assert.deepEqual(exact, expectedExact, `${context}: exact logical/serialization baseline differs`);
  function approximate(a, b, path) {
    if (typeof a === 'number' && typeof b === 'number') {
      assert.ok(Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= 1e-8 * (1 + Math.abs(b)), `${path}: ${a} differs from ${b}`);
    } else if (Array.isArray(a) && Array.isArray(b)) {
      assert.equal(a.length, b.length, `${path}: length differs`);
      a.forEach((v, i) => approximate(v, b[i], `${path}/${i}`));
    } else if (a && b && typeof a === 'object' && typeof b === 'object') {
      assert.deepEqual(Object.keys(a).sort(), Object.keys(b).sort(), `${path}: fields differ`);
      Object.keys(a).forEach(k => approximate(a[k], b[k], `${path}/${k}`));
    } else assert.deepEqual(a, b, path);
  }
  // Counts and descriptors are exact. Only pose/timing-hint samples use tolerance.
  assert.equal(views.length, expectedViews.length, `${context}: view count differs`);
  views.forEach((view, i) => {
    const { frames, ...descriptor } = view, { frames: reference, ...expectedDescriptor } = expectedViews[i];
    assert.deepEqual(descriptor, expectedDescriptor, `${context}: geometry descriptor differs`);
    approximate(frames, reference, `${context}/views/${i}/frames`);
  });
}

const measurements = [];
for (const scenario of baseline.cases) {
  await checkMemory();
  const inputSHA256 = {};
  for (const file of [scenario.package, ...scenario.realizations])
    inputSHA256[file] = createHash('sha256').update(await readFile(join(root, file))).digest('hex');
  if (!record) assert.deepEqual(inputSHA256, scenario.inputSHA256, `${scenario.id}: baseline inputs changed; review before explicitly recording new fixtures`);
  const { expected, inputSHA256: _recordedInputs, ...input } = scenario;
  if (!record) assert.ok(expected, `${scenario.id}: missing frozen observations; use --record only after reviewing the scenario`);
  const native = JSON.parse(run(join(consumer, 'architecture_probe'), [root, JSON.stringify(input)]));
  await checkMemory();
  const wasm = JSON.parse(run(process.execPath, ['--max-old-space-size=512', 'tests/architecture/wasm.mjs', root, JSON.stringify(input)]));
  compare(wasm.observed, native.observed, `${scenario.id}: native/WASM`);
  if (record) {
    scenario.inputSHA256 = inputSHA256;
    scenario.expected = native.observed;
  } else compare(native.observed, scenario.expected, scenario.id, true);
  measurements.push({ id: scenario.id, native: native.metrics, wasm: wasm.metrics });
  console.log(`${scenario.id}: native/WASM definitions, completed witnesses, replay and geometry agree.`);
}
if (record) {
  baseline.originCommit = run('git', ['rev-parse', 'HEAD']).trim();
  await writeFile(fixturePath, JSON.stringify(baseline, null, 2) + '\n');
}
const wasmFile = join(root, 'kernel/generated/twisty.wasm');
await writeFile(join(build, 'measurements.json'), JSON.stringify({
  formatVersion: 1, capturedAt: new Date().toISOString(), originCommit: run('git', ['rev-parse', 'HEAD']).trim(),
  environment: { platform: process.platform, architecture: process.arch, node: process.version,
    nativeBuildType: (await readFile(join(root, 'build/native/CMakeCache.txt'), 'utf8')).match(/^CMAKE_BUILD_TYPE:STRING=(.*)$/m)?.[1],
    wasmBytes: (await stat(wasmFile)).size,
    wasmSHA256: createHash('sha256').update(await readFile(wasmFile)).digest('hex') },
  method: 'One fresh child process per runtime/puzzle, sequential; elapsed times are single observations. Peak RSS includes observation/serialization overhead and runtime startup. Native Debug and WASM optimized builds are not speed-equivalent. Filesystem caches are not cleared. No performance thresholds.',
  measurements,
}, null, 2) + '\n');
console.log(`Architecture baseline ${record ? 'recorded explicitly' : 'verified'}; observations: build/architecture/measurements.json`);
