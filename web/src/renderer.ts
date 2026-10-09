import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TrackballControls } from 'three/addons/controls/TrackballControls.js';
import { copyFloatView, copyIndexView, type MainModule, type Realization, type SceneDescriptor } from '../../kernel/index';
import { DestinationOverlay } from './destinationOverlay';
import type { PieceDestination } from './destinations';
const colors: Record<string, number> = {
  U: 0xf1eee3, R: 0xe76561, F: 0x66c7a0, D: 0xf1cf67, L: 0xeea76d, B: 0x7fa5ed, body: 0x202a35, mechanism: 0x17212a,
};

export class RenderView {
  readonly geometry: InstanceType<MainModule['Geometry']>;
  readonly scene = new THREE.Scene();
  readonly camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
  readonly controls: OrbitControls | TrackballControls;
  private readonly renderer: THREE.WebGLRenderer;
  private readonly descriptor: SceneDescriptor;
  private readonly meshes: THREE.Mesh<THREE.BufferGeometry, THREE.MeshStandardMaterial>[] = [];
  private readonly assets = new Map<string, THREE.BufferGeometry>();
  private readonly textures: THREE.Texture[] = [];
  private readonly labelSprites: THREE.Sprite[] = [];
  private readonly resizeObserver: ResizeObserver;
  private readonly raycaster = new THREE.Raycaster();
  private readonly frameData: Float32Array;
  private readonly destinations: DestinationOverlay;
  private frame = 0;
  private frameId = 0;
  private disposed = false;
  private pointerStart?: { x: number; y: number; id: number; dragged: boolean };

  constructor(
    module: MainModule,
    private readonly host: HTMLElement,
    session: InstanceType<MainModule['Session']>,
    realization: Realization,
    private readonly onPick: (piece: string | undefined) => void,
    private readonly canPick: () => boolean,
    onDestination: (destination: PieceDestination) => void,
  ) {
    const geometry = module.createGeometry(session, JSON.stringify(realization));
    if (!geometry) throw new Error('The geometric interpreter could not be created.');
    this.geometry = geometry;
    this.descriptor = JSON.parse(this.geometry.sceneJSON()) as SceneDescriptor;
    this.frameData = copyFloatView(this.geometry.transforms() as Float32Array);
    try {
      this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    } catch (error) {
      this.geometry.delete();
      throw error;
    }
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.domElement.setAttribute('aria-label', this.descriptor.diagram ? 'Port diagram' : this.descriptor.ambientSpace === 'S2' ? 'Spherical puzzle' : 'Euclidean puzzle');
    this.host.dataset.realizationId = this.descriptor.realizationId;
    this.host.append(this.renderer.domElement);
    if (this.descriptor.diagram) {
      this.camera = new THREE.OrthographicCamera(-6, 6, 4.5, -4.5, 0.1, 100);
      this.camera.position.set(1.5, 0, 15);
    } else {
      this.camera = new THREE.PerspectiveCamera(35, 1, 0.1, 100);
      this.camera.position.set(5, 4.2, 6);
    }
    if (this.descriptor.ambientSpace === 'S2') {
      const controls = new TrackballControls(this.camera, this.renderer.domElement);
      controls.staticMoving = true;
      controls.noPan = true;
      // The application owns keyboard face moves (including D).
      controls.keys = ['', '', ''];
      this.controls = controls;
    } else {
      const controls = new OrbitControls(this.camera, this.renderer.domElement);
      controls.enableDamping = true;
      controls.enablePan = this.descriptor.diagram;
      controls.enableRotate = !this.descriptor.diagram;
      this.controls = controls;
    }
    this.controls.target.set(this.descriptor.diagram ? 1.5 : 0, 0, 0);
    this.controls.minDistance = 5;
    this.controls.maxDistance = 18;
    this.controls.minZoom = 0.6;
    this.controls.maxZoom = 3;
    if (this.controls instanceof TrackballControls) this.alignCameraUp();
    this.controls.update();
    this.scene.add(new THREE.AmbientLight(0xffffff, 2.4));
    const light = new THREE.DirectionalLight(0xffffff, 3.2);
    light.position.set(4, 7, 6);
    this.scene.add(light);
    const fill = new THREE.DirectionalLight(0x8ec3ef, 1.4);
    fill.position.set(-4, -1, -3);
    this.scene.add(fill);

    const positions = copyFloatView(this.geometry.positions() as Float32Array);
    const normals = copyFloatView(this.geometry.normals() as Float32Array);
    const indices = copyIndexView(this.geometry.indices() as Uint32Array);
    for (const asset of this.descriptor.meshAssets) {
      const geometry = new THREE.BufferGeometry();
      const end = asset.positionOffset + asset.vertexCount * 3;
      geometry.setAttribute('position', new THREE.BufferAttribute(positions.slice(asset.positionOffset, end), 3));
      geometry.setAttribute('normal', new THREE.BufferAttribute(normals.slice(asset.positionOffset, end), 3));
      geometry.setIndex(new THREE.BufferAttribute(indices.slice(asset.indexOffset, asset.indexOffset + asset.indexCount), 1));
      geometry.computeBoundingSphere();
      this.assets.set(asset.id, geometry);
    }
    for (const part of this.descriptor.visualParts) {
      const material = new THREE.MeshStandardMaterial({
        color: colors[part.materialBindingId] ?? 0x999999,
        roughness: 0.55,
        metalness: part.role === 'port' ? 0.0 : 0.1,
        side: this.descriptor.diagram ? THREE.DoubleSide : THREE.FrontSide,
      });
      const mesh = new THREE.Mesh(this.assets.get(part.meshAssetId)!, material);
      mesh.matrixAutoUpdate = false;
      mesh.userData = { visualPartId: part.visualPartId, pieceId: part.pieceId, baseColor: colors[part.materialBindingId] };
      this.meshes.push(mesh);
      this.scene.add(mesh);
      if (part.label) {
        const canvas = document.createElement('canvas');
        canvas.width = 256; canvas.height = 256;
        const context = canvas.getContext('2d')!;
        context.fillStyle = '#18242b';
        context.textAlign = 'center';
        context.textBaseline = 'middle';
        const lines = part.label.split(/\s*·\s*/);
        context.font = '500 90px monospace';
        context.fillText(lines[0], 128, lines.length > 1 ? 96 : 128, 220);
        if (lines.length > 1) {
          context.font = '500 58px monospace';
          context.fillText(lines.slice(1).join(' · '), 128, 184, 220);
        }
        const texture = new THREE.CanvasTexture(canvas);
        this.textures.push(texture);
        const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, depthTest: false }));
        const labelScale = part.labelScale ?? 0.76;
        sprite.scale.set(labelScale, labelScale, 1);
        sprite.userData.meshIndex = this.meshes.length - 1;
        this.labelSprites.push(sprite);
        this.scene.add(sprite);
      }
    }
    this.destinations = new DestinationOverlay(module, session, realization, host, this.scene, this.descriptor, this.assets, colors, onDestination, canPick);
    this.updateTransforms();
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(host);
    this.renderer.domElement.addEventListener('pointerdown', this.pointerDown);
    this.renderer.domElement.addEventListener('pointermove', this.pointerMove);
    this.renderer.domElement.addEventListener('pointerup', this.pointerUp);
    this.renderer.domElement.addEventListener('pointercancel', this.pointerCancel);
    this.renderer.domElement.addEventListener('pointerleave', this.pointerLeave);
    this.renderer.domElement.addEventListener('webglcontextlost', this.contextLost);
    this.renderer.domElement.addEventListener('webglcontextrestored', this.contextRestored);
    this.resize();
    this.draw();
  }

  private pointerDown = (event: PointerEvent): void => {
    this.destinations.dismiss();
    this.pointerStart = event.isPrimary && event.button === 0 ? { x: event.clientX, y: event.clientY, id: event.pointerId, dragged: false } : undefined;
  };
  private aim(event: PointerEvent): void {
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.raycaster.setFromCamera(new THREE.Vector2((event.clientX - rect.left) / rect.width * 2 - 1, -(event.clientY - rect.top) / rect.height * 2 + 1), this.camera);
    this.scene.updateMatrixWorld(true);
  }
  private pointerMove = (event: PointerEvent): void => {
    if (this.pointerStart && this.pointerStart.id === event.pointerId && Math.hypot(event.clientX - this.pointerStart.x, event.clientY - this.pointerStart.y) > 5) this.pointerStart.dragged = true;
    if (event.buttons || !this.canPick()) { this.destinations.dismiss(); return; }
    this.aim(event);
    this.destinations.hover(this.raycaster, event.clientX, event.clientY);
  };
  private pointerCancel = (): void => { this.pointerStart = undefined; this.destinations.dismiss(); };
  private pointerLeave = (): void => { this.destinations.clearHover(); };
  private pointerUp = (event: PointerEvent): void => {
    const start = this.pointerStart;
    this.pointerStart = undefined;
    if (!start || start.id !== event.pointerId || event.button !== 0 || start.dragged || !this.canPick() || Math.hypot(event.clientX - start.x, event.clientY - start.y) > 5) return;
    this.aim(event);
    if (this.destinations.pick(this.raycaster, event.clientX, event.clientY)) return;
    const hit = this.raycaster.intersectObjects(this.meshes)[0];
    if (!hit) { this.onPick(undefined); return; }
    // The hit is resolved against this view's sampled frame, never the session's newer state.
    const target = JSON.parse(this.geometry.bindHitJSON(hit.object.userData.visualPartId)) as { status: string; pieceId?: string };
    if (target.status === 'Target' && target.pieceId) this.onPick(target.pieceId);
  };
  private contextLost = (event: Event): void => { event.preventDefault(); };
  private contextRestored = (): void => { this.updateTransforms(); this.resize(); };

  private resize(): void {
    const { width, height } = this.host.getBoundingClientRect();
    if (width < 1 || height < 1) return;
    this.renderer.setSize(width, height, false);
    if (this.camera instanceof THREE.PerspectiveCamera) this.camera.aspect = width / height;
    else {
      const horizontal = Math.max(6.5, 5.1 * width / height);
      const vertical = horizontal * height / width;
      this.camera.left = -horizontal; this.camera.right = horizontal;
      this.camera.top = vertical; this.camera.bottom = -vertical;
    }
    this.camera.updateProjectionMatrix();
    if (this.controls instanceof TrackballControls) this.controls.handleResize();
  }
  private alignCameraUp(): void {
    const direction = this.camera.position.clone().sub(this.controls.target).normalize();
    this.camera.up.projectOnPlane(direction);
    if (this.camera.up.lengthSq() < 1e-12) {
      this.camera.up.set(0, Math.abs(direction.y) < 0.9 ? 1 : 0, Math.abs(direction.y) < 0.9 ? 0 : 1);
      this.camera.up.projectOnPlane(direction);
    }
    // Trackball's drag basis must be orthonormal to follow diagonal drags as
    // precisely as horizontal/vertical ones, including after restoring a view.
    this.camera.up.normalize();
  }
  private updateTransforms(): void {
    // Copy before any other WASM call. Destination storage is reused every frame.
    this.frameData.set(this.geometry.transforms() as Float32Array);
    for (let i = 0; i < this.meshes.length; i++) this.meshes[i].matrix.fromArray(this.frameData, i * 16);
    for (const sprite of this.labelSprites) {
      const offset = sprite.userData.meshIndex * 16;
      sprite.position.set(this.frameData[offset + 12], this.frameData[offset + 13], this.frameData[offset + 14] + 0.02);
    }
    this.frameId++;
    this.host.dataset.frameId = String(this.frameId);
  }
  private draw = (): void => {
    if (this.disposed) return;
    this.controls.update();
    if (this.host.clientWidth > 0) this.renderer.render(this.scene, this.camera);
    this.frame = requestAnimationFrame(this.draw);
  };

  setState(state: string): void { this.destinations.clear(); this.geometry.setStateJSON(state); this.updateTransforms(); }
  prepare(transition: string): void {
    this.destinations.clear();
    const result = JSON.parse(this.geometry.prepareAnimationJSON(transition));
    if (result.status !== 'Prepared') throw new Error(result.diagnostics?.[0]?.message ?? 'Unsupported visual transition.');
  }
  sample(progress: number): void { this.geometry.sample(progress); this.updateTransforms(); }
  setDestinations(piece: string | undefined, destinations: PieceDestination[]): void { this.destinations.setDestinations(piece, destinations); }
  highlight(selected: string | undefined, blocked: string[]): void {
    for (const mesh of this.meshes) {
      const isBlocked = blocked.includes(mesh.userData.pieceId);
      const isSelected = mesh.userData.pieceId === selected;
      mesh.material.emissive.setHex(isBlocked ? 0xff553e : isSelected ? 0x2bd4c6 : 0x000000);
      mesh.material.emissiveIntensity = isBlocked ? 0.5 : isSelected ? 0.3 : 0;
    }
    this.host.dataset.selectedPiece = selected ?? '';
    this.host.dataset.blockedPieces = blocked.join(',');
  }
  cameraState(): unknown { return { position: this.camera.position.toArray(), target: this.controls.target.toArray(), up: this.camera.up.toArray(), zoom: this.camera.zoom }; }
  restoreCamera(value: unknown): void {
    const data = value as { position?: number[]; target?: number[]; up?: number[]; zoom?: number } | undefined;
    if (!data || !Array.isArray(data.position) || !Array.isArray(data.target) || data.position.length !== 3 || data.target.length !== 3) return;
    if (![...data.position, ...data.target, data.zoom ?? 1].every(Number.isFinite)) return;
    const up = data.up ?? [0, 1, 0];
    if (!Array.isArray(up) || up.length !== 3 || !up.every(Number.isFinite) || Math.hypot(...up) < 1e-12) return;
    this.camera.position.fromArray(data.position); this.controls.target.fromArray(data.target);
    this.camera.up.fromArray(up);
    if (this.controls instanceof TrackballControls) this.alignCameraUp();
    this.camera.zoom = Math.min(3, Math.max(0.6, data.zoom ?? 1));
    this.camera.updateProjectionMatrix(); this.controls.update();
  }
  dispose(): void {
    this.disposed = true;
    cancelAnimationFrame(this.frame);
    this.resizeObserver.disconnect(); this.controls.dispose();
    this.renderer.domElement.removeEventListener('pointerdown', this.pointerDown);
    this.renderer.domElement.removeEventListener('pointermove', this.pointerMove);
    this.renderer.domElement.removeEventListener('pointerup', this.pointerUp);
    this.renderer.domElement.removeEventListener('pointercancel', this.pointerCancel);
    this.renderer.domElement.removeEventListener('pointerleave', this.pointerLeave);
    this.renderer.domElement.removeEventListener('webglcontextlost', this.contextLost);
    this.renderer.domElement.removeEventListener('webglcontextrestored', this.contextRestored);
    this.destinations.dispose();
    for (const mesh of this.meshes) mesh.material.dispose();
    for (const sprite of this.labelSprites) sprite.material.dispose();
    for (const texture of this.textures) texture.dispose();
    for (const geometry of this.assets.values()) geometry.dispose();
    this.renderer.dispose(); this.renderer.forceContextLoss(); this.renderer.domElement.remove();
    this.geometry.delete();
  }
}
