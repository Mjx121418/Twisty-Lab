// Renderer-independent value types and borrowed-buffer adapters. No runtime is loaded here.
import type { MainModule as ArtifactModule, Session as NativeSession, GeometryDefinition as NativeGeometryDefinition } from './generated/twisty-artifact.mjs';
export type { NativeSession };
export type RuntimeGeometryInput = { interpretation: string; envelope: string; payload: string };
export type GeometryModule = Pick<ArtifactModule, 'artifactInfoJSON' | 'createGeometryDefinition' | 'createArtifactGeometryWithPayload'> & { kernelInfoJSON?: () => string };

export type State = { placementOf: Record<string, string>; mechanism: Record<string, unknown> };
export type Request = { operation: string; parameters: Record<string, never>; choices?: string[] };
export type Snapshot = {
  puzzleId: string; definitionDigest: string; stateDigest: string; revision: string;
  state: State; solved: boolean; legalRequests: Request[];
  candidateRequests: Request[];
  history: string[]; cursor: number; canUndo: boolean; canRedo: boolean;
};
export type StateSnapshot = Omit<Snapshot, 'legalRequests' | 'candidateRequests'>;
export type TurnIntent = { family: string; targets: Record<string, string>; pathId?: string };
export type ControlDriver = { id: string; piece: string; stops: string[]; placements: Record<string, string> };
export type ControlFamily = { id: string; label?: string; drivers: string[] };
export type ControlCatalog = { version: 1; drivers: ControlDriver[]; families: ControlFamily[] };
export type ControlResult = Result & {
  revision?: string; family?: string; current?: Record<string, string>; pathId?: string;
  targets?: Record<string, string>; destinations?: ControlResult[]; nextOffset?: number; total?: number;
  request?: Request; paths?: { pathId: string; request: Request }[];
};
export type FamilySelection = Result & { revision?: string; pieces?: string[]; families?: string[] };
export type Transition = {
  definitionDigest: string; beforeStateDigest: string; afterStateDigest: string;
  request: Request & { choices: string[] }; beforeState: State; afterState: State;
  witnessVersion: 1;
  pieceActions: { pieceId: string; from: string; to: string; role: string; transport: string; event: string }[];
};
export type Result = {
  status: string; snapshot?: Snapshot | StateSnapshot; transitions?: Transition[]; reasonCode?: string;
  transitionRecords?: string[];
  transition?: Transition; transitionRecord?: string;
  implicatedPieces?: string[]; evidence?: unknown; failureIndex?: number;
  diagnostics?: { reasonCode: string; source: string; message: string }[];
  scramble?: { seed: number; generatorVersion: string; notation: string; requests: string[]; termination: string };
};
export type MeshAsset = {
  id: string; positionOffset: number; vertexCount: number; indexOffset: number; indexCount: number;
  rotationSymmetryOrder?: number;
};
export type VisualPart = {
  visualPartId: string; pieceId: string; portId?: string; meshAssetId: string;
  materialBindingId: string; role: string; label?: string; labelScale?: number;
  /** Local display scale, applied before the intrinsic isometry; defaults to 1. */
  meshScale?: number;
};
/** Immutable prototype meshes in intrinsic coordinates; offsets count scalars. */
export type IntrinsicMeshDescriptor = {
  version: 1; space: GeometrySpace; coordinateSize: 4; scalar: 'float32'; normalModel: 'unit-sphere-tangent';
  meshAssets: (MeshAsset & { normalOffset: number })[];
  visualParts: (VisualPart & { activeCoordinates?: number; cutHalfspaces?: { normal: number[]; minimum: number }[]; boundingCap: { center: number[]; minDot: number } })[];
};
export type IntrinsicMeshes = { descriptor: IntrinsicMeshDescriptor; positions: Float32Array; normals: Float32Array; indices: Uint32Array };
/** Copies borrowed WASM buffers. The returned arrays survive all native handles. */
export function copyIntrinsicMeshes(definition: NativeGeometryDefinition): IntrinsicMeshes {
  const mesh = definition.intrinsicMesh();
  try { return { descriptor: JSON.parse(mesh.descriptorJSON()) as IntrinsicMeshDescriptor,
    positions: copyFloatView(mesh.positions() as Float32Array), normals: copyFloatView(mesh.normals() as Float32Array),
    indices: copyIndexView(mesh.indices() as Uint32Array) }; }
  finally { mesh.delete(); }
}
export type SceneDescriptor = {
  sceneId: string; realizationId: string; definitionDigest: string; requiredCapabilities: string[];
  executionDigest?: string; interpretationDigest?: string; realizationDigest?: string;
  meshAssets: MeshAsset[]; visualParts: VisualPart[]; diagram: boolean;
  space: GeometrySpace; projections: string[]; drawingSpace: string; drawingBufferVersion: number;
  ambientSpace?: 'R3' | 'S2' | 'S3' | 'E2'; sphereRadius?: number; diskAngleDegrees?: number; fidelity?: string;
  dynamicMeshes?: boolean;
  projection?: { viewIsometry: number[]; scale: number; clipRadius: number; outerPieceIds: string[]; pieceDepths: Record<string, number>; clippedTriangles: number };
  axisGuide?: number[][][]; guidePieces?: string[];
  flatSurface?: FlatSurface;
  diskCenters?: Record<string, [number, number, number]>; surfaceTransportVerified?: boolean; catalogTransportVerified?: boolean;
  controlLayout?: Realization['controlLayout'];
};
// Common renderer surface; source and artifact entries provide their own identity pins.
export type GeometryScene = Omit<SceneDescriptor, 'definitionDigest'> & { definitionDigest?: string };
/** Model-qualified layout, not a promise that poses are affine model matrices. */
export type GeometrySpace = {
  id: string; intrinsicDimension: number; coordinateModel: string; metric: string;
  curvature: number; radius?: number; pointCoordinates: number;
  embeddingDimension: number; metricSignature: number[]; homogeneous: boolean;
  isometryRows: number; isometryColumns: number; isometryScalar: 'float64';
  matrixOrder: 'column-major'; meshCoordinates: string;
};
/** Intrinsic quotient data. Displayed images, clipping and cameras belong to the renderer. */
export type FlatSurface = {
  topology: 'torus' | 'klein-bottle' | 'pillowcase' | 'orbifold-333';
  fundamentalBounds: [number, number, number, number];
  chart?: 'rhombus-60' | 'pillowcase-square' | 'orbifold-333-rhombus';
  /** Counterclockwise Cartesian boundary; absent for the original square chart. */
  fundamentalPolygon?: [number, number][];
  /** Chart side vectors. For 333 the translation subgroup uses deckGenerators[0..1] instead. */
  latticeBasis?: [[number, number], [number, number]];
  /** Optional proper lift applied on tiles with odd i+j, before translation. */
  tileReflection?: number[];
  /** Offset of the tile's first vertex from the lattice origin. */
  chartOrigin?: [number, number];
  /** Affine lift applied on odd i+j before translation, preserving handedness. */
  tileHolonomy?: number[];
  /** Distinct cone representatives: four order-2 points on 2222, three order-3 points on 333. */
  conePoints?: [number, number][];
  /** Column-major proper 4x4 lifts: two surface generators, or two translations followed by the 2222 half-turn / 333 third-turn. */
  deckGenerators: [number[], number[]] | [number[], number[], number[]];
  twoSided: true;
  cutRadius?: number;
  diskCenters?: Record<string, [number, number]>;
};
export type HitBinding = {
  status: string; pieceId?: string; portId?: string; operationCandidates?: Request[];
};
export type MovementPresentation = {
  version: 1;
  operations: Record<string, { label: string; accessibleLabel: string; aliases: string[]; angleDegrees?: number[] }>;
  familyOrder: string[];
  familyProbes: Record<string, string>;
  keyboard: Record<string, string[]>;
  interaction: 'moves' | 'destinations' | 'relative' | 'cell-axes';
  instructions: string;
  defaultAlgorithm: string;
};
export type CompiledDefinition = {
  /** Optional authoring output. Never used to determine legality or identity. */
  display?: MovementPresentation;
  kind: 'relational-definition'; formatVersion: 1 | 2 | 3; puzzleId: string; definitionDigest: string;
  ruleModule: 'relational@1';
  pieceTypes: { id: string; placementDomainId: string; localPorts: string[] }[];
  placementDomains: { id: string; placements: { key: string; footprint: string[]; portAttachment: Record<string, { cell: string; attachment: string }> }[] }[];
  pieces: { id: string; type: string; homePlacement: string; portLabels: Record<string, string> }[];
  operations: { id: string; inverse: string; family: string; selectedCells?: string[]; transport: string; }[];
  symmetry?: { members?: number[][]; positionOrbits?: Record<string, { representative: string; stabilizer: number[][]; positions: { id: string }[] }> }; cells: string[];
};
export type PolyhedralModel = {
  vertices: number[][]; ports: Record<string, number[]>; symmetries: number[][];
  surfaceVertices?: number[][];
};
export type CellAxisLayout = { version: 2; kind: 'cell-axes'; groups: {
  id: string; label: string; center: number[];
  categories: { kind: 'face' | 'edge' | 'vertex'; axes: {
    id: string; label: string; direction: number[]; turns: { operation: string; angleDegrees: number }[];
  }[] }[];
}[] };
export type DriverLayout = { version: 1; drivers: Record<string, { angles: number[] }>; paths?: Record<string, Record<string,number>> };
export type ProjectionOptions = { version?: 1; focusCell?: string; viewIsometry?: number[]; scale?: number; clipRadius?: number; guide?: { group: string; axis: string } };
export type Realization = {
  /** Exact source-free documents, supplied by the runtime distribution. */
  runtime?: RuntimeGeometryInput;
  controlLayout?: DriverLayout | CellAxisLayout;
  /** Unit-S3 cut geometry; matrices are SO(4), not homogeneous R3 frames. */
  cellShape?: 'cube' | 'octahedron'; cutDepth?: number; pieceInset?: number;
  domains?: Record<string, { activeCoordinates?: number; cutMembership?: boolean[]; colorNormals?: number[][]; pieceCenter?: number[]; placementFrames: Record<string, number[]>; ports: Record<string, string> }>;
  operationTracks?: Record<string, { cell: number | string; planeU: number[]; planeV: number[]; angle: number }
    | { axis: number[]; fromStop: number; toStop: number } | FamilyRotationBinding>;
  motionFamilies?: { version: 1; families: Record<string, { axis: [number,number,number]; pivot: [number,number,number];
    space: 'world' | 'local'; coordinates: Record<string,number> }> };
  schemaVersion: number; id: string; kind: 'cube-euclidean' | 'cube-port-diagram' | 'cube-spherical' | 'polyhedral-euclidean' | 'polyhedral-port-diagram' | 'polyhedral-spherical' | 'articulated-euclidean' | 'articulated-port-diagram' | 'articulated-spherical' | 'flat-quotient' | 's3-cell-turning';
  flatSurface?: { topology: 'torus' | 'klein-bottle' | 'pillowcase' | 'orbifold-333'; sideLength: number; chart?: 'square' | 'rhombus-60' | 'pillowcase-square' | 'orbifold-333-rhombus'; cutRadius?: number; diskCenters?: Record<string, [number, number]> };
  radius?: number;
  compatibleDefinitionDigest: string; requiredCapabilities: string[];
  models?: Record<string, PolyhedralModel>;
  assets?: Record<string, {
    vertices: number[][]; triangles: number[][];
    /** For S3, one four-coordinate unit tangent outward normal per vertex. */
    normals?: number[][];
    /** Smooth signed normals about the local origin, in any articulated kind. */
    radialNormalTriangles?: number[];
  }>;
  componentsByDomain?: Record<string, ArticulatedComponent[]>;
  /** Named, reusable articulated paths and per-operation placement tables. */
  rotationTracks?: Record<string, RotationTrackStep[] | FamilyRotationBinding>;
  operationTrackTables?: Record<string, OperationTrackTable>;
};
export type RotationTrackStep = {
  axis: [number, number, number]; pivot: [number, number, number]; angle: number; space: 'world' | 'local';
};
export type FamilyRotationBinding = { family: string; from: string; to: string; winding: number; rate?: number };
export type OperationTrackTable = Record<string, Record<string, RotationTrackStep[] | FamilyRotationBinding | string> | { kind: 'transport-regions'; regions: { request: string; path: string }[] }>;
export type ArticulatedComponent = {
  /** Optional body-only filter; ports bind every piece, possibly in several patches. */
  pieceIds?: string[];
  id: string; meshAssetId: string; role: 'body' | 'port'; portId?: string; materialBindingId?: string;
  placementFrames: Record<string, number[]>; symmetries?: number[][];
  operationTracks: OperationTrackTable | string;
};

// Native memory views are borrowed. Every view is copied before the next WASM call.
export function copyFloatView(view: Float32Array): Float32Array { return new Float32Array(view); }
export function copyIndexView(view: Uint32Array): Uint32Array { return new Uint32Array(view); }

export class GeometryInstance<Descriptor extends GeometryScene = SceneDescriptor> {
  readonly definition: NonNullable<ReturnType<GeometryModule['createGeometryDefinition']>>;
  readonly scene: Descriptor;
  readonly matrices: Float32Array;
  /** Owned intrinsic poses, updated even when projection is deferred. */
  readonly isometries: Float64Array;
  drawingIsCurrent = false;
  drawingRevision = 0;
  private animation?: ReturnType<NativeGeometryDefinition['animate']>;
  private readonly destinations = new Map<string, Float32Array>();
  private destinationBytes = 0;
  private frame?: ReturnType<NativeGeometryDefinition['frame']>;
  private options: ProjectionOptions = {};
  positions: Float32Array = new Float32Array();
  normals: Float32Array = new Float32Array();
  indices: Uint32Array = new Uint32Array();
  get projectionOptions(): ProjectionOptions { return structuredClone(this.options); }

  constructor(module: GeometryModule, session: NativeSession, realization: string | RuntimeGeometryInput) {
    if (typeof realization === 'string' && module.kernelInfoJSON) {
      const legacy = JSON.parse(module.kernelInfoJSON());
      if (legacy.geometryAPIVersion !== 1 || legacy.poseBufferVersion !== 1) throw new Error('Unsupported geometry API.');
    }
    const info = JSON.parse(module.artifactInfoJSON()) as { frameBufferVersion: number };
    if (info.frameBufferVersion !== 1) throw new Error('Unsupported geometry buffer version.');
    const definition = typeof realization === 'string' ? module.createGeometryDefinition(session, realization)
      : module.createArtifactGeometryWithPayload(session, realization.interpretation, realization.envelope, realization.payload);
    if (!definition) throw new Error('Cannot create geometry definition.');
    this.definition = definition;
    try {
      this.scene = JSON.parse(definition.sceneJSON()) as Descriptor;
      this.matrices = new Float32Array(this.scene.visualParts.length * 16);
      this.isometries = new Float64Array(this.matrices.length);
      this.setState(session.stateJSON());
    } catch (error) { definition.delete(); throw error; }
  }

  private project(frame: NonNullable<GeometryInstance<Descriptor>['frame']>, options = this.options) {
    const drawing = this.scene.dynamicMeshes
      ? this.definition.projectWithOptions(frame, 'stereographic', JSON.stringify(options))
      : this.definition.project(frame, 'embedding');
    try {
      return { matrices: copyFloatView(drawing.transforms() as Float32Array),
        ...(this.scene.dynamicMeshes ? { scene: JSON.parse(drawing.sceneJSON()) as Descriptor,
          positions: copyFloatView(drawing.positions() as Float32Array), normals: copyFloatView(drawing.normals() as Float32Array),
          indices: copyIndexView(drawing.indices() as Uint32Array) } : {}) };
    } finally { drawing.delete(); }
  }
  private accept(frame: NonNullable<GeometryInstance<Descriptor>['frame']>, options = this.options, project = true): void {
    if (!project) {
      this.isometries.set(frame.isometries() as Float64Array);
      if (this.frame !== frame) this.frame?.delete();
      this.frame = frame;
      this.drawingIsCurrent = false;
      return;
    }
    let result: ReturnType<GeometryInstance<Descriptor>['project']>;
    try { result = this.project(frame, options); } catch (error) { if (frame !== this.frame) frame.delete(); throw error; }
    this.matrices.set(result.matrices);
    this.isometries.set(frame.isometries() as Float64Array);
    if (result.scene) {
      delete this.scene.axisGuide; delete this.scene.guidePieces;
      Object.assign(this.scene, result.scene);
      this.positions = result.positions!; this.normals = result.normals!; this.indices = result.indices!;
    }
    if (this.frame !== frame) this.frame?.delete();
    this.frame = frame;
    this.drawingIsCurrent = true;
    this.drawingRevision++;
  }
  /** Reprojects the current intrinsic frame; does not edit state or interrupt a prepared animation. */
  setProjection(options: ProjectionOptions): void {
    if (!this.scene.dynamicMeshes) throw new Error('This realization has no configurable projection.');
    this.accept(this.frame!, options); this.options = structuredClone(options);
  }
  /** Install the exact state; false retains the previous drawing until explicitly refreshed. */
  setState(stateText: string, project = true): void {
    this.accept(this.definition.frame(stateText), this.options, project);
    this.animation?.delete(); this.animation = undefined;
  }
  prepare(record: string, project = true): number {
    const next = this.definition.animate(record);
    try {
      this.accept(next.evaluate(0), this.options, project);
      const hint = next.durationHint();
      this.animation?.delete(); this.animation = next;
      return hint;
    } catch (error) { next.delete(); throw error; }
  }
  /** Linear progress; apply easing in the controller before calling. */
  evaluate(progress: number): void {
    if (!Number.isFinite(progress)) throw new Error('Animation progress must be finite.');
    if (this.animation) this.accept(this.animation.evaluate(progress));
  }
  /** Advances poses only. Drawing buffers/scene retain their last projected snapshot. */
  evaluatePose(progress: number): void {
    if (!Number.isFinite(progress)) throw new Error('Animation progress must be finite.');
    if (this.animation) this.accept(this.animation.evaluate(progress), this.options, false);
  }
  /** Projects the retained frame, preserving the animation and abstract state. */
  refreshProjection(): void { if (!this.drawingIsCurrent) this.accept(this.frame!); }
  /** Shares all assets and tables; does not disturb the prepared live motion. */
  destination(record: string): Float32Array {
    if (this.scene.dynamicMeshes) throw new Error('Dynamic destinations require GeometryDefinition.animate and projectWithOptions.');
    const cached = this.destinations.get(record);
    if (cached) {
      this.destinations.delete(record); this.destinations.set(record, cached);
      return copyFloatView(cached);
    }
    const animation = this.definition.animate(record);
    try {
      const frame = animation.evaluate(1);
      let matrices: Float32Array;
      try { matrices = this.project(frame).matrices; } finally { frame.delete(); }
      // Only owned buffers are cached, never native handles or borrowed views.
      // Bound memory even for large third-party realizations; callers still
      // receive independent arrays and may freely modify their copy.
      const limit = 2 * 1024 * 1024;
      const bytes = matrices.byteLength + record.length * 2;
      if (bytes <= limit) {
        while (this.destinations.size >= 64 || this.destinationBytes + bytes > limit) {
          const oldest = this.destinations.keys().next().value!;
          this.destinationBytes -= this.destinations.get(oldest)!.byteLength + oldest.length * 2;
          this.destinations.delete(oldest);
        }
        this.destinations.set(record, matrices);
        this.destinationBytes += bytes;
        return copyFloatView(matrices);
      }
      return matrices;
    }
    finally { animation.delete(); }
  }
  resolvePart(id: string): HitBinding { return JSON.parse(this.definition.resolvePartJSON(id)) as HitBinding; }
  dispose(): void {
    this.animation?.delete(); this.animation = undefined;
    this.frame?.delete(); this.frame = undefined;
    this.destinations.clear(); this.destinationBytes = 0;
    this.definition.delete();
  }
}
