import * as THREE from 'three';
import { copyFloatView, type MainModule, type Realization, type SceneDescriptor } from './kernel';
import type { PieceDestination } from './destinations';

type Ghost = THREE.Mesh<THREE.BufferGeometry, THREE.MeshBasicMaterial>;
type DestinationGroup = { options: PieceDestination[]; meshes: Ghost[] };

// This renderer-owned layer never commits a move or samples the live view.
export class DestinationOverlay {
  private preview?: InstanceType<MainModule['Geometry']>;
  private readonly root = document.createElement('div');
  private readonly controls = document.createElement('div');
  private readonly hint = document.createElement('div');
  private readonly chooser = document.createElement('div');
  private readonly groups: DestinationGroup[] = [];
  private readonly ghosts: Ghost[] = [];

  constructor(
    private readonly module: MainModule,
    private readonly session: InstanceType<MainModule['Session']>,
    private readonly realization: Realization,
    private readonly host: HTMLElement,
    private readonly scene: THREE.Scene,
    private readonly descriptor: SceneDescriptor,
    private readonly assets: Map<string, THREE.BufferGeometry>,
    private readonly colors: Record<string, number>,
    private readonly onChoose: (destination: PieceDestination) => void,
    private readonly canChoose: () => boolean,
  ) {
    this.root.className = 'destination-overlay';
    this.controls.className = 'destination-controls';
    this.hint.className = 'destination-hint';
    this.chooser.className = 'destination-chooser';
    this.chooser.setAttribute('role', 'group');
    this.chooser.setAttribute('aria-label', 'Choose a twist for this destination');
    this.root.append(this.controls, this.hint, this.chooser);
    this.host.append(this.root);
    this.clear();
  }

  clear(): void {
    for (const ghost of this.ghosts) { this.scene.remove(ghost); ghost.material.dispose(); }
    this.ghosts.length = 0;
    this.groups.length = 0;
    this.controls.replaceChildren();
    this.dismiss();
    this.root.hidden = true;
    this.host.dataset.destinationCount = '0';
    this.host.dataset.destinationGroups = '0';
    this.host.dataset.destinationPiece = '';
    this.host.dataset.destinationOperations = '';
    this.host.dataset.destinationPartCount = '0';
  }

  setDestinations(pieceId: string | undefined, destinations: PieceDestination[]): void {
    this.clear();
    if (!pieceId) return;
    const parts = this.descriptor.visualParts.map((part, index) => ({ part, index })).filter(({ part }) => part.pieceId === pieceId);
    if (!parts.length) return;
    const byPose = new Map<string, DestinationGroup>();
    if (destinations.length && !this.preview) {
      this.preview = this.module.createGeometry(this.session, JSON.stringify(this.realization)) ?? undefined;
      if (!this.preview) throw new Error('Cannot create destination preview.');
    }
    try {
      for (const option of destinations) {
        if (option.pieceId !== pieceId) continue;
        const prepared = JSON.parse(this.preview!.prepareAnimationJSON(option.transitionRecord));
        if (prepared.status !== 'Prepared') throw new Error(prepared.diagnostics?.[0]?.message ?? 'Unsupported destination.');
        this.preview!.sample(1);
        const transforms = copyFloatView(this.preview!.transforms() as Float32Array);
        const matrices = parts.map(({ index }) => transforms.slice(index * 16, index * 16 + 16));
        const poseKey = matrices.map((matrix) => Array.from(matrix, (value) => Math.round(value * 100000)).join(',')).join(';');
        const existing = byPose.get(poseKey);
        if (existing) { existing.options.push(option); continue; }
        const group: DestinationGroup = { options: [option], meshes: [] };
        byPose.set(poseKey, group);
        this.groups.push(group);
        for (let i = 0; i < parts.length; i++) {
          const { part } = parts[i];
          const material = new THREE.MeshBasicMaterial({
            color: part.role === 'body' ? 0x9fe6cf : this.colors[part.materialBindingId] ?? 0x9fe6cf,
            transparent: true,
            opacity: part.role === 'body' ? 0.22 : 0.48,
            // Destinations often coincide with occupied surfaces, including the
            // back of the puzzle. Draw and pick them consistently through it.
            depthTest: false,
            depthWrite: false,
            side: THREE.DoubleSide,
            forceSinglePass: true,
          });
          const ghost = new THREE.Mesh(this.assets.get(part.meshAssetId)!, material);
          ghost.matrixAutoUpdate = false;
          ghost.matrix.fromArray(matrices[i]);
          ghost.renderOrder = part.role === 'body' ? 10 : 11;
          ghost.userData = { group, baseOpacity: material.opacity, pieceId, visualPartId: part.visualPartId };
          group.meshes.push(ghost);
          this.ghosts.push(ghost);
          this.scene.add(ghost);
        }
      }
    } catch (error) { this.clear(); throw error; }
    this.root.hidden = false;
    this.host.dataset.destinationPiece = pieceId;
    this.host.dataset.destinationCount = String(destinations.length);
    this.host.dataset.destinationGroups = String(this.groups.length);
    this.host.dataset.destinationOperations = destinations.map((option) => option.request.operation).join(',');
    this.host.dataset.destinationPartCount = String(this.ghosts.length);
    const status = document.createElement('span');
    status.setAttribute('role', 'status');
    status.textContent = destinations.length ? 'Click a translucent destination' : 'No legal twists for this piece';
    this.controls.append(status);
    if (destinations.length) {
      // The same choices remain reachable by keyboard or if a destination is
      // outside the viewport. They do not add labels or shapes to the puzzle.
      const details = document.createElement('details');
      const summary = document.createElement('summary');
      summary.textContent = `${destinations.length} twists`;
      const list = document.createElement('div');
      list.className = 'destination-options';
      list.setAttribute('role', 'group');
      list.setAttribute('aria-label', `Twists for ${pieceId}`);
      for (const option of destinations) list.append(this.button(option));
      details.append(summary, list);
      this.controls.append(details);
    }
  }

  private button(option: PieceDestination): HTMLButtonElement {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = option.request.operation;
    button.setAttribute('aria-label', `Twist ${option.request.operation}`);
    button.addEventListener('click', () => { if (this.canChoose()) this.onChoose(option); });
    const emphasize = (): void => this.emphasize(this.groups.filter((group) => group.options.includes(option)));
    button.addEventListener('pointerenter', emphasize);
    button.addEventListener('focus', emphasize);
    button.addEventListener('pointerleave', () => this.emphasize([]));
    button.addEventListener('blur', () => this.emphasize([]));
    return button;
  }

  private hits(raycaster: THREE.Raycaster): DestinationGroup[] {
    const hits = raycaster.intersectObjects(this.ghosts);
    if (!hits.length) return [];
    // Shared or overlapping surfaces need a choice rather than an arbitrary
    // request determined by mesh insertion order.
    return [...new Set(hits.filter((hit) => hit.distance - hits[0].distance < 0.035).map((hit) => hit.object.userData.group as DestinationGroup))];
  }

  pick(raycaster: THREE.Raycaster, clientX: number, clientY: number): boolean {
    this.dismiss();
    const groups = this.hits(raycaster);
    if (!groups.length) return false;
    const options = groups.flatMap((group) => group.options);
    if (options.length === 1) this.onChoose(options[0]);
    else {
      const title = document.createElement('span');
      title.textContent = 'Choose a twist';
      const close = document.createElement('button');
      close.type = 'button';
      close.textContent = '×';
      close.setAttribute('aria-label', 'Close destination chooser');
      close.addEventListener('click', () => this.dismiss());
      const list = document.createElement('div');
      list.className = 'destination-options';
      for (const option of options) list.append(this.button(option));
      this.chooser.replaceChildren(title, close, list);
      this.position(this.chooser, clientX, clientY, 230, 160);
      this.chooser.hidden = false;
      list.querySelector('button')?.focus({ preventScroll: true });
    }
    return true;
  }

  hover(raycaster: THREE.Raycaster, clientX: number, clientY: number): void {
    if (!this.chooser.hidden) return;
    const groups = this.hits(raycaster);
    this.emphasize(groups);
    this.hint.hidden = !groups.length;
    if (groups.length) {
      this.hint.textContent = groups.flatMap((group) => group.options.map((option) => option.request.operation)).join(' / ');
      this.position(this.hint, clientX + 12, clientY + 12, 180, 40);
    }
  }

  private position(element: HTMLElement, x: number, y: number, width: number, height: number): void {
    const bounds = this.host.getBoundingClientRect();
    element.style.left = `${Math.max(8, Math.min(bounds.width - width - 8, x - bounds.left))}px`;
    element.style.top = `${Math.max(8, Math.min(bounds.height - height - 8, y - bounds.top))}px`;
  }

  private emphasize(groups: DestinationGroup[]): void {
    for (const group of this.groups) for (const mesh of group.meshes) mesh.material.opacity = groups.includes(group) ? Math.min(0.8, mesh.userData.baseOpacity + 0.25) : mesh.userData.baseOpacity;
  }

  dismiss(): void {
    this.clearHover();
    this.chooser.hidden = true;
    this.chooser.replaceChildren();
    this.emphasize([]);
  }

  clearHover(): void {
    this.hint.hidden = true;
    if (this.chooser.hidden) this.emphasize([]);
  }

  dispose(): void { this.clear(); this.preview?.delete(); this.root.remove(); }
}
