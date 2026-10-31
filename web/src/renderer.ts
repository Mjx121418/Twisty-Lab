import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TrackballControls } from 'three/addons/controls/TrackballControls.js';
import { GeometryInstance, copyFloatView, copyIndexView, type MainModule, type NativeSession, type Realization, type SceneDescriptor, type ProjectionOptions } from './kernel';
import { DestinationOverlay } from './destinationOverlay';
import type { PieceDestination } from './destinations';
import { S3GpuProjection } from './s3Gpu';
import { S3MaterialPasses, type S3Mesh } from './s3Materials';
const colors: Record<string, number> = {
  U: 0xf1eee3, R: 0xe76561, F: 0x66c7a0, D: 0xf1cf67, L: 0xeea76d, B: 0x7fa5ed, body: 0x202a35, mechanism: 0x17212a, hardware: 0xaeb8c3,
};

export class RenderView {
  readonly geometry: GeometryInstance;
  readonly scene = new THREE.Scene();
  readonly camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
  readonly controls: OrbitControls | TrackballControls;
  private readonly renderer: THREE.WebGLRenderer;
  private readonly descriptor: SceneDescriptor;
  private readonly meshes: S3Mesh[] = [];
  private readonly materials: THREE.MeshStandardMaterial[] = [];
  private readonly materialPasses?: S3MaterialPasses;
  private readonly assets = new Map<string, THREE.BufferGeometry>();
  private readonly gpu?: S3GpuProjection;
  private readonly pickingMeshes: THREE.Mesh[] = [];
  private uploadedDrawing = 0;
  private readonly selectionEdges = new Map<string, THREE.EdgesGeometry>();
  private readonly selectionOutlines = new Map<number, THREE.LineSegments>();
  private readonly selectionMaterial = new THREE.LineBasicMaterial({ color: 0xffdb80, transparent: true, depthWrite: false });
  private readonly textures: THREE.Texture[] = [];
  private readonly labelSprites: THREE.Sprite[] = [];
  private readonly resizeObserver: ResizeObserver;
  private readonly raycaster = new THREE.Raycaster();
  private readonly frameData: Float32Array;
  private readonly destinations: DestinationOverlay;
  private shellVisible = true;
  private selected: readonly string[] = [];
  private blocked: string[] = [];
  private readonly guide = new THREE.LineSegments(new THREE.BufferGeometry(), new THREE.LineBasicMaterial({ color: 0xffdf8b, depthTest: false, transparent: true }));
  private frame = 0;
  private needsRender = true;
  private invalidate = (): void => { this.needsRender = true; };
  private frameId = 0;
  private disposed = false;
  private pointerStart?: { x: number; y: number; id: number; dragged: boolean };

  constructor(
    module: MainModule,
    private readonly host: HTMLElement,
    session: NativeSession,
    realization: Realization,
    private readonly onPick: (piece: string | undefined) => void,
    private readonly canPick: () => boolean,
    onDestination: (destination: PieceDestination) => void,
    initialCameraScale = 1,
  ) {
    const geometry = new GeometryInstance(module, session, realization.runtime ?? JSON.stringify(realization));
    this.geometry = geometry;
    if (realization.controlLayout?.version === 2) {
      const group = realization.controlLayout.groups[0];
      geometry.setProjection({ guide: { group: group.id, axis: group.categories[0].axes[0].id } });
    }
    this.descriptor = this.geometry.scene;
    this.frameData = copyFloatView(this.geometry.matrices);
    try {
      this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    } catch (error) {
      this.geometry.dispose();
      throw error;
    }
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.domElement.setAttribute('aria-label', this.descriptor.diagram ? 'Port diagram' : this.descriptor.ambientSpace === 'S3' ? 'Stereographic S³ puzzle' : this.descriptor.ambientSpace === 'S2' ? 'Spherical puzzle' : 'Euclidean puzzle');
    this.host.dataset.realizationId = this.descriptor.realizationId;
    this.host.append(this.renderer.domElement);
    const cameraScale = this.descriptor.diagram ? 1 : this.descriptor.ambientSpace === 'S3' ? 0.62 : initialCameraScale;
    if (this.descriptor.diagram) {
      this.camera = new THREE.OrthographicCamera(-6, 6, 4.5, -4.5, 0.1, 100);
      this.camera.position.set(1.5, 0, 15);
    } else {
      this.camera = new THREE.PerspectiveCamera(35, 1, this.descriptor.ambientSpace === 'S3' ? .005 : .1, 100);
      this.camera.position.set(5, 4.2, 6).multiplyScalar(cameraScale);
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
    this.controls.minDistance = this.descriptor.ambientSpace === 'S3' ? .05 : 5 * cameraScale;
    this.controls.maxDistance = 18 * cameraScale;
    this.controls.minZoom = 0.6;
    this.controls.maxZoom = 3;
    if (this.controls instanceof TrackballControls) this.alignCameraUp();
    this.controls.update();
    this.controls.addEventListener('change', this.invalidate);
    this.scene.add(new THREE.AmbientLight(0xffffff, 2.4));
    const light = new THREE.DirectionalLight(0xffffff, 3.2);
    light.position.set(4, 7, 6);
    this.scene.add(light);
    const fill = new THREE.DirectionalLight(0x8ec3ef, 1.4);
    fill.position.set(-4, -1, -3);
    this.scene.add(fill);

    const positions = this.descriptor.dynamicMeshes ? this.geometry.positions : copyFloatView(this.geometry.definition.positions() as Float32Array);
    const normals = this.descriptor.dynamicMeshes ? this.geometry.normals : copyFloatView(this.geometry.definition.normals() as Float32Array);
    const indices = this.descriptor.dynamicMeshes ? this.geometry.indices : copyIndexView(this.geometry.definition.indices() as Uint32Array);
    for (const asset of this.descriptor.meshAssets) {
      const geometry = new THREE.BufferGeometry();
      const end = asset.positionOffset + asset.vertexCount * 3;
      geometry.setAttribute('position', new THREE.BufferAttribute(positions.slice(asset.positionOffset, end), 3));
      geometry.setAttribute('normal', new THREE.BufferAttribute(normals.slice(asset.positionOffset, end), 3));
      geometry.setIndex(new THREE.BufferAttribute(indices.slice(asset.indexOffset, asset.indexOffset + asset.indexCount), 1));
      geometry.computeBoundingSphere();
      this.assets.set(asset.id, geometry);
    }
    const materialColors = { ...colors };
    for (const part of this.descriptor.visualParts) {
      if (/^#[0-9a-f]{6}$/i.test(part.materialBindingId)) materialColors[part.materialBindingId] = parseInt(part.materialBindingId.slice(1), 16);
      const material = new THREE.MeshStandardMaterial({
        color: materialColors[part.materialBindingId] ?? 0x999999,
        roughness: 0.55,
        metalness: part.role === 'port' ? 0.0 : 0.1,
        side: this.descriptor.diagram || this.descriptor.dynamicMeshes ? THREE.DoubleSide : THREE.FrontSide,
      });
      const mesh = new THREE.Mesh(this.assets.get(part.meshAssetId)!, material);
      mesh.matrixAutoUpdate = false;
      mesh.userData = { visualPartId: part.visualPartId, pieceId: part.pieceId, baseColor: material.color.getHex(), role: part.role, meshAssetId: part.meshAssetId };
      this.meshes.push(mesh);
      this.materials.push(material);
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
    if (this.descriptor.ambientSpace === 'S3') {
      this.gpu = new S3GpuProjection(this.geometry, this.meshes, this.assets, realization.cutDepth);
      this.materialPasses = new S3MaterialPasses();
      for (const [index, mesh] of this.meshes.entries()) {
        const picking = new THREE.Mesh(this.assets.get(mesh.userData.meshAssetId)!, this.materials[index]);
        picking.userData = mesh.userData;
        picking.userData.meshIndex = this.pickingMeshes.length;
        picking.matrixAutoUpdate = false;
        this.pickingMeshes.push(picking);
      }
    }
    this.destinations = new DestinationOverlay(this.geometry, host, this.scene, this.descriptor, this.assets, materialColors, onDestination, canPick);
    this.scene.add(this.guide); this.guide.renderOrder = 25;
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
    this.invalidate();
    this.destinations.dismiss();
    this.pointerStart = event.isPrimary && event.button === 0 ? { x: event.clientX, y: event.clientY, id: event.pointerId, dragged: false } : undefined;
  };
  private aim(event: PointerEvent): void {
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.raycaster.setFromCamera(new THREE.Vector2((event.clientX - rect.left) / rect.width * 2 - 1, -(event.clientY - rect.top) / rect.height * 2 + 1), this.camera);
    this.scene.updateMatrixWorld(true);
  }
  private pointerMove = (event: PointerEvent): void => {
    this.invalidate();
    if (this.pointerStart && this.pointerStart.id === event.pointerId && Math.hypot(event.clientX - this.pointerStart.x, event.clientY - this.pointerStart.y) > 5) this.pointerStart.dragged = true;
    if (event.buttons || !this.canPick()) { this.destinations.dismiss(); return; }
    this.aim(event);
    this.destinations.hover(this.raycaster, event.clientX, event.clientY);
  };
  private pointerCancel = (): void => { this.invalidate(); this.pointerStart = undefined; this.destinations.dismiss(); };
  private pointerLeave = (): void => { this.invalidate(); this.destinations.clearHover(); };
  private pointerUp = (event: PointerEvent): void => {
    const start = this.pointerStart;
    this.pointerStart = undefined;
    if (!start || start.id !== event.pointerId || event.button !== 0 || start.dragged || !this.canPick() || Math.hypot(event.clientX - start.x, event.clientY - start.y) > 5) return;
    this.aim(event);
    if (this.destinations.pick(this.raycaster, event.clientX, event.clientY)) return;
    if (this.gpu) {
      this.geometry.refreshProjection();
      this.updateTransforms();
    }
    const candidates = this.gpu ? this.pickingMeshes.filter(mesh => this.gpu!.opacityRanges[mesh.userData.meshIndex].max === 1) : this.meshes;
    const hit = this.raycaster.intersectObjects(candidates).find(hit => !this.gpu || this.gpu.opaqueAt(hit.point));
    if (!hit) { this.onPick(undefined); return; }
    // Resolve the renderer's picked visual part to its persistent identity.
    const target = this.geometry.resolvePart(hit.object.userData.visualPartId) as { status: string; pieceId?: string };
    if (target.status === 'Target' && target.pieceId) this.onPick(target.pieceId);
  };
  private contextLost = (event: Event): void => { event.preventDefault(); };
  private contextRestored = (): void => { this.updateTransforms(); this.resize(); };

  private resize(): void {
    this.invalidate();
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
  private uploadDrawing(): void {
    if (this.uploadedDrawing === this.geometry.drawingRevision) return;
    for (const asset of this.descriptor.meshAssets) {
      const geometry = this.assets.get(asset.id)!;
      const end = asset.positionOffset + asset.vertexCount * 3;
      for (const [name, data] of [['position', this.geometry.positions.subarray(asset.positionOffset, end)],
        ['normal', this.geometry.normals.subarray(asset.positionOffset, end)]] as const) {
        const attribute = geometry.getAttribute(name) as THREE.BufferAttribute;
        if (attribute.array.length === data.length) { attribute.array.set(data); attribute.needsUpdate = true; }
        else geometry.setAttribute(name, new THREE.BufferAttribute(data.slice(), 3));
      }
      const data = this.geometry.indices.subarray(asset.indexOffset, asset.indexOffset + asset.indexCount);
      if (geometry.index?.array.length === data.length) { geometry.index.array.set(data); geometry.index.needsUpdate = true; }
      else geometry.setIndex(new THREE.BufferAttribute(data.slice(), 1));
      geometry.computeBoundingSphere();
    }
    const segments: number[] = [];
    for (const path of this.descriptor.axisGuide ?? []) for (let i = 1; i < path.length; i++) segments.push(...path[i-1], ...path[i]);
    this.guide.geometry.dispose(); this.guide.geometry = new THREE.BufferGeometry();
    this.guide.geometry.setAttribute('position', new THREE.Float32BufferAttribute(segments, 3));
    this.uploadedDrawing = this.geometry.drawingRevision;
    this.gpu?.captureDrawing();
  }
  private updateTransforms(): void {
    if (this.descriptor.dynamicMeshes) {
      this.uploadDrawing();
      if (this.gpu && !this.gpu.update()) {
        this.geometry.refreshProjection();
        this.uploadDrawing();
        if (!this.gpu.update()) throw new Error('Cannot reuse current clipped drawing.');
      }
      this.highlight(this.selected, this.blocked);
    }
    // The presentation helper owns this JS snapshot; GPU storage is reused.
    this.frameData.set(this.geometry.matrices);
    if (!this.gpu) for (let i = 0; i < this.meshes.length; i++) {
      this.meshes[i].matrix.fromArray(this.frameData, i * 16);
      this.meshes[i].matrixWorldNeedsUpdate = true;
    }
    for (const [index, outline] of this.selectionOutlines) outline.matrix.copy(this.meshes[index].matrix);
    for (const sprite of this.labelSprites) {
      const offset = sprite.userData.meshIndex * 16;
      sprite.position.set(this.frameData[offset + 12], this.frameData[offset + 13], this.frameData[offset + 14] + 0.02);
    }
    this.frameId++;
    this.host.dataset.frameId = String(this.frameId);
    if (this.gpu) {
      this.host.dataset.vertexTransform = 'gpu';
      this.host.dataset.drawingRevision = String(this.geometry.drawingRevision);
    }
  }
  private draw = (): void => {
    if (this.disposed) return;
    this.controls.update();
    // S³ poses and materials change only on input or animation samples.
    // Let an idle dense model rest instead of redrawing its transparent layers.
    if (this.host.clientWidth > 0 && (!this.gpu || this.needsRender)) {
      this.renderer.render(this.scene, this.camera);
      this.needsRender = false;
    }
    this.frame = requestAnimationFrame(this.draw);
  };

  setState(state: string): void { this.destinations.clear(); this.geometry.setState(state, !this.gpu); this.updateTransforms(); }
  prepare(transition: string): void {
    this.destinations.clear();
    this.geometry.prepare(transition, !this.gpu);
  }
  sample(progress: number): void {
    if (this.gpu) this.geometry.evaluatePose(progress); else this.geometry.evaluate(progress);
    this.updateTransforms();
  }
  setDestinations(piece: string | undefined, destinations: PieceDestination[]): void { this.destinations.setDestinations(piece, destinations); }
  setProjection(options: ProjectionOptions): void { this.geometry.setProjection(options); this.updateTransforms(); }
  focusCell(cell: string): void { this.setProjection({ ...this.geometry.projectionOptions, viewIsometry: undefined, focusCell: cell }); }
  setCellGuide(group: string, axis: string): void {
    const options = this.geometry.projectionOptions;
    if (options.guide?.group === group && options.guide.axis === axis) return;
    this.setProjection({ ...options, guide: { group, axis } });
  }
  toggleOuterShell(): void { this.showOuterShell(!this.shellVisible); }
  showOuterShell(visible: boolean): void { this.shellVisible = visible; this.highlight(this.selected, this.blocked); }
  get fadeSettings() { return this.gpu?.fadeSettings; }
  setFadeStartRadius(radius: number): void {
    if (!this.gpu) return;
    this.gpu.setFadeStartRadius(radius);
    this.updateTransforms();
  }
  setFadeEndRadius(radius: number): void {
    if (!this.gpu) return;
    this.gpu.setFadeEndRadius(radius);
    this.updateTransforms();
  }
  highlight(selected: readonly string[], blocked: string[]): void {
    this.invalidate();
    this.selected = [...selected]; this.blocked = [...blocked];
    const selection = new Set(selected);
    for (let index = 0; index < this.meshes.length; index++) {
      const mesh = this.meshes[index];
      const material = this.materials[index];
      const isBlocked = blocked.includes(mesh.userData.pieceId);
      const isSelected = selection.has(mesh.userData.pieceId);
      material.color.setHex(mesh.userData.baseColor).multiplyScalar(selected.length > 0 && !isSelected && !isBlocked ? 0.65 : 1);
      material.emissive.setHex(isBlocked ? 0xff553e : isSelected ? 0xffc75f : 0x000000);
      material.emissiveIntensity = isBlocked ? 0.5 : isSelected ? 0.45 : 0;
      // Opaque selected surfaces join the transparent pass at full opacity,
      // after the destination ghosts. Depth testing still hides rear faces.
      const depth = (this.gpu?.pieceDepths ?? this.descriptor.projection?.pieceDepths)?.[mesh.userData.pieceId] ?? -1;
      const fade = Math.max(0, Math.min(1, (depth - 0.1) / 0.35));
      const opacity = this.gpu?.opacityRanges[index];
      const outer = opacity ? opacity.min < 1 : fade > 0;
      mesh.visible = opacity ? opacity.max > 0 && (!outer || this.shellVisible) : depth <= 0.1 || this.shellVisible;
      material.opacity = opacity ? 1 : 1 - 0.92 * fade * fade * (3 - 2 * fade);
      material.depthWrite = !outer;
      if (!isSelected && (this.gpu ? this.gpu.guidePieces.has(mesh.userData.pieceId) : this.descriptor.guidePieces?.includes(mesh.userData.pieceId))) {
        material.emissive.setHex(0x8f7850); material.emissiveIntensity = 0.15;
      }
      if (material.transparent !== (isSelected || outer)) {
        material.transparent = isSelected || outer;
        material.needsUpdate = true;
      }
      this.materialPasses?.sync(mesh, material);
      mesh.renderOrder = isSelected ? (mesh.userData.role === 'body' ? 20 : 21) : 0;
      let outline = this.selectionOutlines.get(index);
      if (isSelected && !outline && !this.descriptor.dynamicMeshes) {
        const asset = mesh.userData.meshAssetId as string;
        let edges = this.selectionEdges.get(asset);
        if (!edges) {
          // Keep patch boundaries and sharp corners, without tracing the
          // internal triangles of a tessellated spherical surface.
          edges = new THREE.EdgesGeometry(mesh.geometry, 30);
          this.selectionEdges.set(asset, edges);
        }
        outline = new THREE.LineSegments(edges, this.selectionMaterial);
        outline.matrixAutoUpdate = false;
        outline.matrix.copy(mesh.matrix);
        outline.renderOrder = 22;
        this.selectionOutlines.set(index, outline);
        this.scene.add(outline);
      }
      if (outline) outline.visible = isSelected;
    }
    for (const sprite of this.labelSprites) sprite.renderOrder = selection.has(this.meshes[sprite.userData.meshIndex].userData.pieceId) ? 23 : 0;
    this.host.dataset.selectedPiece = selected.length === 1 ? selected[0] : '';
    this.host.dataset.selectedPieces = JSON.stringify(selected);
    this.host.dataset.selectedCount = String(selected.length);
    this.host.dataset.blockedPieces = blocked.join(',');
    if (this.gpu) {
      this.host.dataset.fadeStartRadius = String(this.gpu.fadeSettings.startRadius);
      this.host.dataset.fadeEndRadius = String(this.gpu.fadeSettings.endRadius);
    }
  }
  cameraState(): unknown { return { position: this.camera.position.toArray(), target: this.controls.target.toArray(), up: this.camera.up.toArray(), zoom: this.camera.zoom, projection: this.geometry.projectionOptions, shellVisible: this.shellVisible, fadeStartRadius: this.gpu?.fadeSettings.startRadius, fadeEndRadius: this.gpu?.fadeSettings.endRadius }; }
  restoreCamera(value: unknown): void {
    const data = value as { position?: number[]; target?: number[]; up?: number[]; zoom?: number; projection?: ProjectionOptions; shellVisible?: boolean; fadeStartRadius?: number; fadeEndRadius?: number } | undefined;
    if (!data || !Array.isArray(data.position) || !Array.isArray(data.target) || data.position.length !== 3 || data.target.length !== 3) return;
    if (![...data.position, ...data.target, data.zoom ?? 1].every(Number.isFinite)) return;
    const up = data.up ?? [0, 1, 0];
    if (!Array.isArray(up) || up.length !== 3 || !up.every(Number.isFinite) || Math.hypot(...up) < 1e-12) return;
    if (this.descriptor.dynamicMeshes && data.projection) this.setProjection(data.projection);
    if (typeof data.shellVisible === 'boolean') this.showOuterShell(data.shellVisible);
    if (typeof data.fadeEndRadius === 'number') this.setFadeEndRadius(data.fadeEndRadius);
    if (typeof data.fadeStartRadius === 'number') this.setFadeStartRadius(data.fadeStartRadius);
    this.camera.position.fromArray(data.position); this.controls.target.fromArray(data.target);
    this.camera.up.fromArray(up);
    if (this.controls instanceof TrackballControls) this.alignCameraUp();
    this.camera.zoom = Math.min(3, Math.max(0.6, data.zoom ?? 1));
    this.camera.updateProjectionMatrix(); this.controls.update(); this.invalidate();
  }
  dispose(): void {
    this.disposed = true;
    cancelAnimationFrame(this.frame);
    this.resizeObserver.disconnect(); this.controls.removeEventListener('change', this.invalidate); this.controls.dispose();
    this.renderer.domElement.removeEventListener('pointerdown', this.pointerDown);
    this.renderer.domElement.removeEventListener('pointermove', this.pointerMove);
    this.renderer.domElement.removeEventListener('pointerup', this.pointerUp);
    this.renderer.domElement.removeEventListener('pointercancel', this.pointerCancel);
    this.renderer.domElement.removeEventListener('pointerleave', this.pointerLeave);
    this.renderer.domElement.removeEventListener('webglcontextlost', this.contextLost);
    this.renderer.domElement.removeEventListener('webglcontextrestored', this.contextRestored);
    this.destinations.dispose();
    this.gpu?.dispose();
    this.materialPasses?.dispose();
    this.guide.geometry.dispose(); this.guide.material.dispose();
    this.selectionMaterial.dispose();
    for (const edges of this.selectionEdges.values()) edges.dispose();
    for (const material of this.materials) material.dispose();
    for (const sprite of this.labelSprites) sprite.material.dispose();
    for (const texture of this.textures) texture.dispose();
    for (const geometry of this.assets.values()) geometry.dispose();
    this.renderer.dispose(); this.renderer.forceContextLoss(); this.renderer.domElement.remove();
    this.geometry.dispose();
  }
}
