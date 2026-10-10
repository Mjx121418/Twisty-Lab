import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { loadRuntime, type MainModule, type SceneDescriptor } from './kernel';
import assemblyDefinition from '../../packages/gear-compound/gear-compound-definition.json?raw';
import assemblyRealization from '../../packages/gear-compound/gear-compound-euclidean.json?raw';
import driverDefinition from '../../packages/gear-compound/drivers-definition.json?raw';
import driverRealization from '../../packages/gear-compound/drivers-euclidean.json?raw';
import './gear-construction.css';

const host = document.querySelector<HTMLDivElement>('#view')!;
const status = document.querySelector<HTMLParagraphElement>('#status')!;
const mode = document.querySelector<HTMLSelectElement>('#mode')!;
const family = document.querySelector<HTMLSelectElement>('#family')!;
const group = document.querySelector<HTMLSelectElement>('#group')!;
const piece = document.querySelector<HTMLSelectElement>('#piece')!;
const explode = document.querySelector<HTMLInputElement>('#explode')!;
const colors: Record<string, number> = { U: 0xfff5dd, D: 0xffdc5a, R: 0xed706d, L: 0xffad52, F: 0x71d9ae, B: 0x729cff };
const groups: Record<string, Set<string>> = {
  UD: new Set(['center/U', 'center/D', 'edge/FR', 'edge/FL', 'edge/BR', 'edge/BL']),
  RL: new Set(['center/R', 'center/L', 'edge/UF', 'edge/UB', 'edge/DF', 'edge/DB']),
  FB: new Set(['center/F', 'center/B', 'edge/UR', 'edge/UL', 'edge/DR', 'edge/DL']),
};
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 1.5));
host.appendChild(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x192d37);
scene.add(new THREE.HemisphereLight(0xffffff, 0x516572, 2.2));
const light = new THREE.DirectionalLight(0xffffff, 2.8);
light.position.set(4, 7, 5); scene.add(light);
const fill = new THREE.DirectionalLight(0xb8d9ff, 1.3);
fill.position.set(-4, 1, -4); scene.add(fill);
const camera = new THREE.PerspectiveCamera(38, 1, .1, 40);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = false;
let frameRequested = false;
function render(): void {
  if (frameRequested) return;
  frameRequested = true;
  requestAnimationFrame(() => { frameRequested = false; renderer.render(scene, camera); });
}
function resetCamera(): void {
  camera.position.set(5, 4.2, 6); controls.target.set(0, 0, 0); controls.update(); render();
}
resetCamera();
controls.addEventListener('change', render);
new ResizeObserver(() => {
  const width = host.clientWidth, height = host.clientHeight;
  renderer.setSize(width, height, false); camera.aspect = width / height;
  camera.updateProjectionMatrix(); render();
}).observe(host);

type GeometryHandle = NonNullable<ReturnType<MainModule['createGeometry']>>;
type Display = {
  session: InstanceType<MainModule['Session']>; geometry: GeometryHandle;
  descriptor: SceneDescriptor; assets: THREE.BufferGeometry[];
  meshes: THREE.Mesh<THREE.BufferGeometry, THREE.MeshStandardMaterial>[];
  frames: THREE.Matrix4[]; directions: THREE.Vector3[];
};
let runtime: MainModule;
let display: Display | undefined;

function disposeDisplay(): void {
  if (!display) return;
  for (const mesh of display.meshes) { scene.remove(mesh); mesh.material.dispose(); }
  for (const asset of display.assets) asset.dispose();
  display.geometry.delete(); display.session.delete(); display = undefined;
}

function update(): void {
  if (!display) return;
  const drivers = mode.value === 'drivers';
  const visiblePieces = new Set<string>();
  display.meshes.forEach((mesh, index) => {
    const part = display!.descriptor.visualParts[index];
    const kind = part.pieceId.split('/')[0];
    mesh.visible = (!drivers || part.role === 'body') &&
      (drivers || family.value === 'all' || kind === family.value) &&
      (!drivers || group.value === 'all' || groups[group.value].has(part.pieceId)) &&
      (piece.value === 'all' || piece.value === part.pieceId);
    mesh.matrix.copy(display!.frames[index]);
    const displacement = display!.directions[index].clone().multiplyScalar(Number(explode.value) * 1.2);
    mesh.matrix.elements[12] += displacement.x; mesh.matrix.elements[13] += displacement.y; mesh.matrix.elements[14] += displacement.z;
    if (mesh.visible) visiblePieces.add(part.pieceId);
  });
  host.dataset.visiblePieces = String(visiblePieces.size);
  host.dataset.mode = mode.value;
  const selected = piece.value !== 'all' ? ` · ${piece.value}` : '';
  status.textContent = drivers ? `${visiblePieces.size} parent gears visible${selected} · big gears: 20 teeth · small gears: 6 teeth` :
    `${visiblePieces.size} of 38 pieces visible${selected} · 6 faces + 8 vertices + 12 small gears + 12 extra edges`;
  render();
}

function loadDisplay(): void {
  disposeDisplay();
  const drivers = mode.value === 'drivers';
  const session = new runtime.Session(drivers ? driverDefinition : assemblyDefinition);
  const geometry = runtime.createGeometry(session, drivers ? driverRealization : assemblyRealization);
  if (!geometry) { session.delete(); throw new Error('The kernel could not load the construction.'); }
  const descriptor = JSON.parse(geometry.sceneJSON()) as SceneDescriptor;
  const positions = geometry.positions() as Float32Array;
  const normals = geometry.normals() as Float32Array;
  const indices = geometry.indices() as Uint32Array;
  const matrices = geometry.transforms() as Float32Array;
  const assets = new Map<string, THREE.BufferGeometry>();
  for (const asset of descriptor.meshAssets) {
    const buffer = new THREE.BufferGeometry();
    buffer.setAttribute('position', new THREE.BufferAttribute(positions.slice(asset.positionOffset, asset.positionOffset + asset.vertexCount * 3), 3));
    buffer.setAttribute('normal', new THREE.BufferAttribute(normals.slice(asset.positionOffset, asset.positionOffset + asset.vertexCount * 3), 3));
    buffer.setIndex(new THREE.BufferAttribute(indices.slice(asset.indexOffset, asset.indexOffset + asset.indexCount), 1));
    assets.set(asset.id, buffer);
  }
  const frames: THREE.Matrix4[] = [], directions: THREE.Vector3[] = [];
  const meshes = descriptor.visualParts.map((part, index) => {
    const kind = part.pieceId.split('/')[0];
    const parentBig = drivers && kind === 'center';
    const material = new THREE.MeshStandardMaterial({
      color: drivers ? parentBig ? colors[part.pieceId.split('/')[1]] : 0x8ce0c4 :
        colors[part.materialBindingId] ?? (kind === 'extra-edge' ? 0x506871 : 0x293c45),
      roughness: .75, metalness: .05,
      transparent: parentBig, opacity: parentBig ? .38 : 1, depthWrite: !parentBig,
      side: THREE.DoubleSide,
    });
    const mesh = new THREE.Mesh(assets.get(part.meshAssetId)!, material);
    const matrix = new THREE.Matrix4().fromArray(matrices, index * 16);
    frames.push(matrix);
    const localDirection = kind === 'center' ? new THREE.Vector3(0, 1, 0) :
      kind === 'corner' ? new THREE.Vector3(1, 1, 1) : new THREE.Vector3(0, 1, 1);
    directions.push(localDirection.transformDirection(matrix));
    mesh.matrixAutoUpdate = false; mesh.userData.visualPartId = part.visualPartId;
    scene.add(mesh); return mesh;
  });
  display = { session, geometry, descriptor, assets: [...assets.values()], meshes, frames, directions };
  const ids = [...new Set(descriptor.visualParts.map(part => part.pieceId))].sort();
  piece.replaceChildren(new Option('All pieces', 'all'), ...ids.map(id => new Option(id, id)));
  host.dataset.pieceCount = String(ids.length);
  family.disabled = drivers; group.disabled = !drivers;
  update();
}

const raycaster = new THREE.Raycaster();
let pointerStart: { x: number; y: number } | undefined;
renderer.domElement.addEventListener('pointerdown', event => { pointerStart = { x: event.clientX, y: event.clientY }; });
renderer.domElement.addEventListener('pointerup', event => {
  if (!display || !pointerStart || Math.hypot(event.clientX - pointerStart.x, event.clientY - pointerStart.y) > 5) return;
  const rect = renderer.domElement.getBoundingClientRect();
  raycaster.setFromCamera(new THREE.Vector2(2 * (event.clientX - rect.left) / rect.width - 1, 1 - 2 * (event.clientY - rect.top) / rect.height), camera);
  scene.updateMatrixWorld(true);
  const hit = raycaster.intersectObjects(display.meshes.filter(mesh => mesh.visible), false)[0];
  if (!hit) return;
  const binding = JSON.parse(display.geometry.bindHitJSON(hit.object.userData.visualPartId)) as { pieceId: string };
  piece.value = binding.pieceId; update();
});
mode.addEventListener('change', () => { try { loadDisplay(); } catch (error) { status.textContent = String(error); host.dataset.error = 'true'; } });
for (const input of [family, group]) input.addEventListener('input', () => { piece.value = 'all'; update(); });
piece.addEventListener('input', () => { family.value = 'all'; group.value = 'all'; update(); });
explode.addEventListener('input', update);
document.querySelector('#reset')!.addEventListener('click', resetCamera);
window.addEventListener('pagehide', () => { disposeDisplay(); controls.dispose(); renderer.dispose(); });

try { runtime = await loadRuntime(); loadDisplay(); }
catch (error) { status.textContent = String(error); host.dataset.error = 'true'; }
