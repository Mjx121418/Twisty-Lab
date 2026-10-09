import { createKernel, KernelSession, copyFloatView, copyIndexView } from '../../index.js';

const module = await createKernel();
const canvas = document.querySelector('#view');
const context = canvas.getContext('2d');
const palette = { U: '#e4e1d8', R: '#e76561', F: '#66c7a0', D: '#e8c451', L: '#eea76d', B: '#7fa5ed', body: '#202a35' };
let session, geometry, descriptor, positions, indices, transforms;
let busy = false, selected, blocked = [], hits = [];
let installEpoch = 0;
const assets = new Map();
const supported = new Set(['triangle-meshes', 'rigid-transforms']);

function draw() {
  context.clearRect(0, 0, canvas.width, canvas.height);
  hits = [];
  const scale = Math.min(canvas.width / 13, canvas.height / 10);
  descriptor.visualParts.forEach((part, partIndex) => {
    const asset = assets.get(part.meshAssetId);
    const offset = partIndex * 16;
    const point = (index) => {
      const start = asset.positionOffset + index * 3;
      const [x, y, z] = positions.subarray(start, start + 3);
      const px = transforms[offset] * x + transforms[offset + 4] * y + transforms[offset + 8] * z + transforms[offset + 12];
      const py = transforms[offset + 1] * x + transforms[offset + 5] * y + transforms[offset + 9] * z + transforms[offset + 13];
      return [(px - 1.5) * scale + canvas.width / 2, canvas.height / 2 - py * scale];
    };
    for (let i = asset.indexOffset; i < asset.indexOffset + asset.indexCount; i += 3) {
      const triangle = [point(indices[i]), point(indices[i + 1]), point(indices[i + 2])];
      context.beginPath();
      triangle.forEach(([x, y], vertex) => vertex ? context.lineTo(x, y) : context.moveTo(x, y));
      context.closePath();
      context.fillStyle = palette[part.materialBindingId] ?? '#999';
      context.fill();
      context.strokeStyle = blocked.includes(part.pieceId) ? '#ce3724' : selected === part.pieceId ? '#176b88' : '#60746f';
      context.lineWidth = blocked.includes(part.pieceId) || selected === part.pieceId ? 3 : 1;
      context.stroke();
      hits.push({ triangle, part });
    }
  });
}
function sample(progress) {
  geometry.sample(progress);
  transforms.set(geometry.transforms()); // Copy the borrowed view before another kernel call.
  draw();
}
function refresh() {
  const snapshot = session.snapshot();
  document.querySelector('#digest').textContent = snapshot.stateDigest;
  document.querySelector('#undo').disabled = busy || !snapshot.canUndo;
  document.querySelector('#redo').disabled = busy || !snapshot.canRedo;
  document.querySelector('#puzzle').disabled = busy;
  document.querySelector('#algorithm button').disabled = busy;
  const legal = new Set(snapshot.legalRequests.map((request) => request.operation));
  const controls = document.querySelector('#moves');
  controls.replaceChildren();
  for (const operation of session.definition.operations) {
    if (!Object.entries(operation.pieceGuards ?? {}).every(([piece, placement]) => snapshot.state.placementOf[piece] === placement)) continue;
    const button = document.createElement('button');
    button.textContent = operation.id;
    button.disabled = busy;
    button.className = legal.has(operation.id) ? '' : 'blocked';
    button.addEventListener('click', () => play(() => session.move(operation.id, snapshot.revision)));
    controls.append(button);
  }
}
async function install(puzzle) {
  const epoch = ++installEpoch;
  busy = true;
  document.querySelectorAll('button, select').forEach((element) => { element.disabled = true; });
  document.querySelector('#status').textContent = 'Loading…';
  const [source, realization] = await Promise.all([
    fetch(`./data/${puzzle}/definition.json`).then((response) => response.text()),
    fetch(`./data/${puzzle}/${puzzle === 'helicopter' ? 'helicopter' : 'cube'}-port-diagram.json`).then((response) => response.text()),
  ]);
  if (epoch !== installEpoch) return;
  const next = new KernelSession(module, source);
  let view;
  try {
    view = module.createGeometry(next.native, realization);
    if (!view) throw new Error('Cannot create geometric interpreter.');
    const scene = JSON.parse(view.sceneJSON());
    if (!scene.diagram || scene.requiredCapabilities.some((capability) => !supported.has(capability))) throw new Error('This renderer supports flat triangle-mesh diagrams.');
    geometry?.delete(); session?.dispose();
    session = next; geometry = view; descriptor = scene;
  } catch (error) { view?.delete(); next.dispose(); throw error; }
  positions = copyFloatView(geometry.positions());
  indices = copyIndexView(geometry.indices());
  transforms = copyFloatView(geometry.transforms());
  assets.clear(); descriptor.meshAssets.forEach((asset) => assets.set(asset.id, asset));
  selected = undefined; blocked = [];
  document.querySelector('#selection').textContent = 'No piece selected';
  document.querySelector('#notation').value = puzzle === 'helicopter' ? 'UF_ab UL_af' : "R U R' U'";
  document.querySelector('#status').textContent = 'Ready';
  busy = false;
  refresh(); draw();
}
async function play(command) {
  if (busy) return;
  busy = true; refresh();
  try {
    const result = command();
    blocked = result.implicatedPieces ?? [];
    document.querySelector('#status').textContent = result.reasonCode ?? result.diagnostics?.[0]?.message ?? result.status;
    for (const record of result.transitionRecords ?? []) {
      const prepared = JSON.parse(geometry.prepareAnimationJSON(record));
      if (prepared.status !== 'Prepared') throw new Error('Unsupported visual transition.');
      await new Promise((resolve) => {
        const start = performance.now();
        function tick(now) {
          const progress = Math.min(1, (now - start) / 180);
          sample(progress);
          if (progress === 1) resolve(); else requestAnimationFrame(tick);
        }
        requestAnimationFrame(tick);
      });
    }
  } catch (error) { document.querySelector('#status').textContent = String(error); }
  finally {
    geometry.setStateJSON(session.stateText()); transforms.set(geometry.transforms()); draw();
    busy = false; refresh();
  }
}
canvas.addEventListener('click', (event) => {
  if (busy) return;
  const bounds = canvas.getBoundingClientRect();
  const point = [(event.clientX - bounds.left) * canvas.width / bounds.width, (event.clientY - bounds.top) * canvas.height / bounds.height];
  const cross = (a, b, p) => (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
  const hit = [...hits].reverse().find(({ triangle: [a, b, c] }) => {
    const signs = [cross(a, b, point), cross(b, c, point), cross(c, a, point)];
    return signs.every((value) => value >= 0) || signs.every((value) => value <= 0);
  });
  if (!hit) return;
  const binding = JSON.parse(geometry.bindHitJSON(hit.part.visualPartId));
  selected = binding.pieceId;
  document.querySelector('#selection').textContent = `Selected ${selected}${binding.portId ? ` · port ${binding.portId}` : ''}`;
  draw();
});
document.querySelector('#algorithm').addEventListener('submit', (event) => {
  event.preventDefault();
  const revision = session.snapshot().revision;
  play(() => session.run(document.querySelector('#notation').value, 'interactive', revision));
});
document.querySelector('#undo').addEventListener('click', () => play(() => session.undo()));
document.querySelector('#redo').addEventListener('click', () => play(() => session.redo()));
document.querySelector('#puzzle').addEventListener('change', (event) => {
  install(event.target.value).catch((error) => {
    busy = false;
    document.querySelector('#status').textContent = String(error);
    if (session) refresh();
  });
});
window.addEventListener('pagehide', () => { geometry?.delete(); session?.dispose(); });
await install('cube3');
