// Run after npm run package:kernel. Single observations, not a statistical benchmark.
import { readFile } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';
import { execFileSync } from 'node:child_process';
import { createKernel, KernelSession, KernelController, GeometryInstance } from '../build/kernel/index.js';

const module = await createKernel();
const args = process.argv.slice(2);
const baseline = args[0] === '--baseline' ? args.splice(0, 2)[1] : undefined;
if (process.argv[2] === '--baseline' && !baseline) throw new Error('Supply a baseline git ref.');
const names = args.length ? args : ['helicopter', 'melty-a', 'melty-b', 'melty-c', 'melty-d'];
const read = path => baseline ? execFileSync('git', ['show', `${baseline}:${path}`],
  { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 }) : readFile(new URL(`../${path}`, import.meta.url), 'utf8');
const samples = [];
function timed(f) {
  const start = performance.now(), value = f();
  return { value, ms: Math.round((performance.now() - start) * 1000) / 1000 };
}
for (const id of names) {
  const source = await read(`packages/${id}/package.json`);
  const realization = await read(`packages/${id}/${id}-euclidean.json`);
  const loaded = timed(() => new KernelSession(module, source));
  const session = loaded.value;
  let controller, geometry;
  try {
    const setup = timed(() => new KernelController(module, session)); controller = setup.value;
    const family = controller.controls.families.find(f => f.id === (id === 'helicopter' ? 'UF' : 'U'));
    const phase = family ? timed(() => controller.familyState(family.id)) : undefined;
    const driver = family && controller.controls.drivers.find(d => d.id === family.drivers[0]);
    const target = driver?.stops.find(s => s !== phase.value.current[driver.id]);
    const state = timed(() => session.stateSnapshot());
    const preview = family ? timed(() => controller.planTo({ family: family.id, targets: { [driver.id]: target } })) : undefined;
    const move = timed(() => session.plan({ operation: id === 'helicopter' ? 'UF_ab' : 'U', parameters: {} }));
    const full = timed(() => session.snapshot()); // First full query at this revision, including all request legality.
    const scene = timed(() => new GeometryInstance(module, session.native, realization)); geometry = scene.value;
    samples.push({ puzzle: id, sessionLoadMs: loaded.ms, controllerSetupMs: setup.ms, phaseQueryMs: phase?.ms,
      stateSnapshotMs: state.ms, oneDestinationMs: preview?.ms, previewStatus: preview?.value.status, oneMoveMs: move.ms,
      fullSnapshotMs: full.ms, geometryCompileMs: scene.ms, packageBytes: Buffer.byteLength(source),
      realizationBytes: Buffer.byteLength(realization) });
  } finally { geometry?.dispose(); controller?.dispose(); session.dispose(); }
}
console.log(JSON.stringify({ runtime: 'WASM, one process, single observations', baseline, samples }, null, 2));
