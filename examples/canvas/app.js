import { createKernel, KernelSession, copyFloatView, copyIndexView } from '../../index.js';

const module = await createKernel();
const canvas = document.querySelector('#view');
const context = canvas.getContext('2d');
const palette = { U: '#e4e1d8', R: '#e76561', F: '#66c7a0', D: '#e8c451', L: '#eea76d', B: '#7fa5ed', A: '#e76561', C: '#f1cf67', body: '#202a35', ink: '#16252d' };
let session, geometry, descriptor, positions, indices, transforms;
let busy = false, selected, blocked = [], hits = [];
let installEpoch = 0;
const assets = new Map();
const supported = new Set(['triangle-meshes', 'rigid-transforms', 'quotient-surface']);
const identity = () => [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
function multiply(a,b) {
  const out=Array(16).fill(0);
  for(let c=0;c<4;c++)for(let r=0;r<4;r++)for(let k=0;k<4;k++)out[c*4+r]+=a[k*4+r]*b[c*4+k];
  return out;
}
function inverseRigid(m) {
  const out=identity();
  for(let c=0;c<3;c++)for(let r=0;r<3;r++)out[c*4+r]=m[r*4+c];
  for(let r=0;r<3;r++)out[12+r]=-out[r]*m[12]-out[4+r]*m[13]-out[8+r]*m[14];
  return out;
}
function power(m,n) { let out=identity();if(n<0)m=inverseRigid(m);for(let i=0;i<Math.abs(n);i++)out=multiply(out,m);return out; }
let images=[identity()], clipPixels;

function draw() {
  context.clearRect(0, 0, canvas.width, canvas.height);
  hits = [];
  const bounds=descriptor.flatSurface?.fundamentalBounds;
  const scale = bounds ? Math.min(canvas.width/(bounds[2]*1.2),canvas.height/(bounds[3]*1.2)) : Math.min(canvas.width / 13, canvas.height / 10);
  const project=(x,y)=>[(x-(bounds?bounds[2]/2:1.5))*scale+canvas.width/2,canvas.height/2-(y-(bounds?bounds[3]/2:0))*scale];
  context.save();clipPixels=undefined;
  if(bounds){
    const [left,top]=project(bounds[0],bounds[3]),[right,bottom]=project(bounds[2],bounds[1]);
    clipPixels=[left,top,right,bottom];context.beginPath();context.rect(left,top,right-left,bottom-top);context.clip();
  }
  const parts=descriptor.visualParts.map((part,index)=>({part,index}));
  if(bounds)parts.sort((a,b)=>layer(a.part)-layer(b.part));
  function layer(part){return part.materialBindingId==='ink'?2:part.role==='port'?1:0;}
  for(const image of images) parts.forEach(({part,index:partIndex}) => {
    const asset = assets.get(part.meshAssetId);
    const matrix=multiply(image,transforms.subarray(partIndex*16,partIndex*16+16));
    const point = (index) => {
      const start = asset.positionOffset + index * 3;
      const [x, y, z] = positions.subarray(start, start + 3);
      const px = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
      const py = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
      return project(px,py);
    };
    if(bounds)context.beginPath();
    for (let i = asset.indexOffset; i < asset.indexOffset + asset.indexCount; i += 3) {
      const triangle = [point(indices[i]), point(indices[i + 1]), point(indices[i + 2])];
      if(!bounds)context.beginPath();
      triangle.forEach(([x, y], vertex) => vertex ? context.lineTo(x, y) : context.moveTo(x, y));
      context.closePath();
      context.fillStyle = bounds && part.materialBindingId==='B' ? '#66c7a0' : bounds && part.materialBindingId==='D' ? '#7fa5ed' : palette[part.materialBindingId] ?? '#999';
      if(!bounds)context.fill();
      context.strokeStyle = blocked.includes(part.pieceId) ? '#ce3724' : selected === part.pieceId ? '#176b88' : '#60746f';
      context.lineWidth = blocked.includes(part.pieceId) || selected === part.pieceId ? 3 : 1;
      if(!bounds)context.stroke();
      hits.push({ triangle, part });
    }
    // Fill a flat patch as one path so Canvas antialiasing does not expose
    // the internal triangulation as hairline gaps between filled triangles.
    if(bounds)context.fill();
  });
  context.restore();
  if(clipPixels){context.strokeStyle='#758ca5';context.lineWidth=2;context.strokeRect(clipPixels[0],clipPixels[1],clipPixels[2]-clipPixels[0],clipPixels[3]-clipPixels[1]);}
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
  const candidates = new Set(snapshot.candidateRequests.map((request) => request.operation));
  for (const operation of session.definition.operations) {
    if (!candidates.has(operation.id)) continue;
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
    fetch(`./data/${puzzle}/relational-package.json`).then((response) => response.text()),
    fetch(`./data/${puzzle}/${puzzle.startsWith('flat-') ? 'flat-quotient.json' : `${puzzle === 'helicopter' ? 'relational-helicopter' : puzzle.startsWith('gear') ? `relational-${puzzle}` : puzzle === 'bagua' ? 'relational-bagua' : 'relational-cube'}-port-diagram.json`}`).then((response) => response.text()),
  ]);
  if (epoch !== installEpoch) return;
  const next = new KernelSession(module, source);
  let view;
  try {
    view = module.createGeometry(next.native, realization);
    if (!view) throw new Error('Cannot create geometric interpreter.');
    const scene = JSON.parse(view.sceneJSON());
    if ((!scene.diagram&&!scene.flatSurface) || scene.requiredCapabilities.some((capability) => !supported.has(capability))) throw new Error('This renderer supports flat triangle-mesh diagrams and square quotients.');
    geometry?.delete(); session?.dispose();
    session = next; geometry = view; descriptor = scene;
  } catch (error) { view?.delete(); next.dispose(); throw error; }
  positions = copyFloatView(geometry.positions());
  indices = copyIndexView(geometry.indices());
  transforms = copyFloatView(geometry.transforms());
  images=[identity()];
  if(descriptor.flatSurface){const [a,b]=descriptor.flatSurface.deckGenerators;images=[];for(let y=-1;y<=1;y++)for(let x=-1;x<=1;x++)images.push(multiply(power(a,x),power(b,y)));}
  assets.clear(); descriptor.meshAssets.forEach((asset) => assets.set(asset.id, asset));
  selected = undefined; blocked = [];
  document.querySelector('#selection').textContent = 'No piece selected';
  document.querySelector('#notation').value = puzzle.startsWith('flat-') ? "A B A' B'" : puzzle === 'helicopter' ? 'UF_ab UL_af' :
    puzzle === 'bagua' ? "U+ R' L' D2 R L U-" : puzzle === 'gear-plain' ? "Rq Uq Rq' Uq'" : puzzle.startsWith('gear') ? "R R'" : "R U R' U'";
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
  if(clipPixels&&(point[0]<clipPixels[0]||point[0]>clipPixels[2]||point[1]<clipPixels[1]||point[1]>clipPixels[3]))return;
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
