import createModule, { type MainModule } from './generated/twisty.mjs';
export type { MainModule } from './generated/twisty.mjs';

export type KernelInfo = {
  apiVersion: number; version: string; documentKinds: string[]; frameBufferVersion: number;
  languageAPIVersion: number;
  authoringLibraries?: string[];
  catalogKinds?: string[];
  definitionFormatVersion?: number; sessionSchemaVersions?: number[]; sessionFeatures?: string[];
  ruleModules: string[]; realizationKinds: string[]; geometryCapabilities: string[];
};
export type KernelOptions = { wasmURL?: string | URL; locateFile?: (filename: string, directory: string) => string };
export async function createKernel(options: KernelOptions = {}): Promise<MainModule> {
  const locateFile = options.locateFile ?? ((filename: string, directory: string) =>
    filename.endsWith('.wasm') && options.wasmURL ? String(options.wasmURL) : directory + filename);
  const module = await createModule({ locateFile });
  const info = JSON.parse(module.kernelInfoJSON()) as KernelInfo;
  if (info.apiVersion !== 4 || info.frameBufferVersion !== 1) throw new Error('Unsupported kernel API or frame-buffer version.');
  return module;
}

export type State = { placementOf: Record<string, string>; mechanism: Record<string, unknown> };
export type Request = { operation: string; parameters: Record<string, never>; choices?: string[] };
export type Snapshot = {
  puzzleId: string; definitionDigest: string; stateDigest: string; revision: string;
  state: State; solved: boolean; legalRequests: Request[];
  candidateRequests: Request[];
  history: string[]; cursor: number; canUndo: boolean; canRedo: boolean;
};
export type Transition = {
  definitionDigest: string; beforeStateDigest: string; afterStateDigest: string;
  request: Request & { choices: string[] }; beforeState: State; afterState: State;
  witnessVersion: 1;
  pieceActions: { pieceId: string; from: string; to: string; role: string; transport: string; event: string }[];
};
export type Result = {
  status: string; snapshot?: Snapshot; transitions?: Transition[]; reasonCode?: string;
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
};
export type SceneDescriptor = {
  sceneId: string; realizationId: string; definitionDigest: string; requiredCapabilities: string[];
  meshAssets: MeshAsset[]; visualParts: VisualPart[]; diagram: boolean;
  ambientSpace?: 'R3' | 'S2' | 'E2'; sphereRadius?: number; diskAngleDegrees?: number; fidelity?: string;
  flatSurface?: FlatSurface;
  diskCenters?: Record<string, [number, number, number]>; surfaceTransportVerified?: boolean; catalogTransportVerified?: boolean;
};
/** Intrinsic quotient data. Displayed images, clipping and cameras belong to the renderer. */
export type FlatSurface = {
  topology: 'torus' | 'klein-bottle' | 'pillowcase';
  fundamentalBounds: [number, number, number, number];
  chart?: 'rhombus-60' | 'pillowcase-square';
  /** Counterclockwise Cartesian boundary; absent for the original square chart. */
  fundamentalPolygon?: [number, number][];
  /** Chart side vectors. A displayed tile (i,j) is translated by i*a+j*b. */
  latticeBasis?: [[number, number], [number, number]];
  /** Optional proper lift applied on tiles with odd i+j, before translation. */
  tileReflection?: number[];
  /** Offset of the tile's first vertex from the lattice origin. */
  chartOrigin?: [number, number];
  /** Affine lift applied on odd i+j before translation, preserving handedness. */
  tileHolonomy?: number[];
  /** Locations of the four order-2 cone points for the pillowcase. */
  conePoints?: [number, number][];
  /** Column-major proper 4x4 lifts: two surface generators, or two translations and the pillowcase involution. */
  deckGenerators: [number[], number[]] | [number[], number[], number[]];
  twoSided: true;
  cutRadius?: number;
  diskCenters?: Record<string, [number, number]>;
};
export type HitBinding = {
  status: string; pieceId?: string; portId?: string; operationCandidates?: Request[];
};
export type CompiledDefinition = {
  kind: 'relational-definition'; formatVersion: 1; puzzleId: string; definitionDigest: string;
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
export type Realization = {
  schemaVersion: number; id: string; kind: 'cube-euclidean' | 'cube-port-diagram' | 'cube-spherical' | 'polyhedral-euclidean' | 'polyhedral-port-diagram' | 'polyhedral-spherical' | 'articulated-euclidean' | 'articulated-port-diagram' | 'articulated-spherical' | 'flat-quotient';
  flatSurface?: { topology: 'torus' | 'klein-bottle' | 'pillowcase'; sideLength: number; chart?: 'square' | 'rhombus-60' | 'pillowcase-square'; cutRadius?: number; diskCenters?: Record<string, [number, number]> };
  radius?: number;
  compatibleDefinitionDigest: string; requiredCapabilities: string[];
  models?: Record<string, PolyhedralModel>;
  assets?: Record<string, {
    vertices: number[][]; triangles: number[][];
    /** Smooth signed normals about the local origin, in any articulated kind. */
    radialNormalTriangles?: number[];
  }>;
  componentsByDomain?: Record<string, ArticulatedComponent[]>;
};
export type RotationTrackStep = {
  axis: [number, number, number]; pivot: [number, number, number]; angle: number; space: 'world' | 'local';
};
export type ArticulatedComponent = {
  /** Optional body-only filter; ports bind every piece, possibly in several patches. */
  pieceIds?: string[];
  id: string; meshAssetId: string; role: 'body' | 'port'; portId?: string; materialBindingId?: string;
  placementFrames: Record<string, number[]>; symmetries?: number[][];
  operationTracks: Record<string, Record<string, RotationTrackStep[]>>;
};

// Experimental version 1. Numeric placements are resolved through preview.bindings.
// This pure preview/query API remains separate from persisted session packages.
export type Predicate = boolean
  | { at: { piece: string; placements: string[] } }
  | { chosen: { domain: string; moves: string[] } }
  | { request: string[] } | { ref: string } | { not: Predicate }
  | { all: Predicate[] } | { any: Predicate[] }
  | { implies: [Predicate, Predicate] } | { iff: [Predicate, Predicate] }
  | { count: { min: number; max: number; of: Predicate[] } }
  | { occupiedBy: { placement: string; pieces: string[] } };
export type PuzzleSource = {
  kind: 'relational-puzzle'; languageVersion: 1; id: string;
  domains: {
    id: string; placements: string[]; pieces: { id: string; initial: string }[];
    moves: { id: string; support: string[]; transport: Record<string, string>;
      pieces?: string[]; requires?: Predicate; event?: string }[];
  }[];
  predicates?: Record<string, Predicate>;
  relations?: { id: string; predicate: Predicate }[];
  invariant?: Predicate; goal?: Predicate;
  requests: { id: string; relation: Predicate;
    profiles?: { id: string; choices: Record<string, string>; when?: Predicate }[]; defaultProfile?: string }[];
};
export type CubeCatalog = {
  kind: 'relational-cube-bindings'; version: 1; puzzleId: string;
  faceLabels: string[]; symmetry: number[][]; resources: string[];
  domains: { id: string; pieces: string[]; placements: string[]; localPorts: string[] }[];
  pieces: Record<string, { domain: string; home: string; portLabels: Record<string, string> }>;
  placements: Record<string, { resources: string[]; ports: Record<string, { cell: string; attachment: string }> }>;
  requests: Record<string, { inverse: string; family: string; event: string; profile: string }>;
};
export type PortCatalog = Omit<CubeCatalog, 'kind' | 'faceLabels' | 'symmetry' | 'requests'> & {
  kind: 'relational-port-bindings'; symmetry?: number[][];
  requests: Record<string, { inverse: string; family: string; event: string; profile: string; unavailablePlacements: string[] }>;
};
export type ArticulatedCatalog = Omit<PortCatalog, 'kind'> & {
  kind: 'relational-articulated-bindings';
  events: Record<string, { domain: string; request: string }>;
  moveInverses: Record<string, Record<string, string>>;
};
export type GearSource = {
  kind: 'relational-gear-prototypes'; formatVersion: 1; puzzleId: string; gearedEdges: string[];
};
export function createGearSource(module: MainModule, source: GearSource): {
  source: PuzzleSource; catalog: ArticulatedCatalog;
} {
  return JSON.parse(module.gearModuleJSON(JSON.stringify(source))) as {
    source: PuzzleSource; catalog: ArticulatedCatalog;
  };
}
export type PuzzlePackage = {
  kind: 'relational-package'; formatVersion: 1;
  source: PuzzleSource; catalog: CubeCatalog | PortCatalog | ArticulatedCatalog;
};
export type PuzzleDefinition = CompiledDefinition & {
  kind: 'relational-definition'; formatVersion: 1; predicateVersion: 1;
  source: PuzzleSource; catalog: CubeCatalog | PortCatalog | ArticulatedCatalog;
  program: ProgramPreview['program']; bindings: ProgramPreview['bindings'];
};
export function createCubeSource(module: MainModule, bandaged = false): {
  source: PuzzleSource; catalog: CubeCatalog;
} {
  return JSON.parse(module.cubeModuleJSON(bandaged)) as { source: PuzzleSource; catalog: CubeCatalog };
}
export type ProgramPreview = {
  kind: 'relational-preview'; experimentalVersion: 1; predicateVersion: 1;
  bindings: {
    puzzleId: string; pieces: string[]; placements: string[]; events: string[];
    choiceSlots: string[]; choiceValues: string[][];
    requests: { id: string; profiles: { id: string; choices: string[]; whenRoot: number }[]; defaultProfile: string | null }[];
  };
  program: { pieceCount: number; placementCount: number; initialPlacements: number[];
    finiteSets: number[][]; predicateNodes: Record<string, unknown>[]; childReferences: number[];
    transportMaps: [number, number][][]; actionRecords: { piece: number; map: number; selectionRoot: number; event: number }[];
    requestPrograms: { choiceSizes: number[]; firstAction: number; actionCount: number; relationRoot: number }[];
    invariantRoot: number; goalRoot: number };
};
export type ProgramQuery = { state: number[] } & (
  { op: 'validate' | 'goal' }
  | { op: 'plan'; request: string; choices: string[] }
  | { op: 'bind'; request: string; profile?: string }
  | { op: 'discover'; request: string; candidateLimit: number; resultLimit: number });
export type ProgramResult =
  | { status: 'Invalid'; diagnostics?: { code: string; message: string }[] }
  | { status: 'Valid' | 'ChoiceRequired' }
  | { status: 'Evaluated'; solved: boolean }
  | { status: 'Selected'; choices: string[] }
  | { status: 'Discovered'; choices: string[][]; examined: number; complete: boolean }
  | { status: 'Rejected'; reason: 'relation' | 'conflicting-writes' | 'target-invariant' }
  | { status: 'Permitted'; transition: { request: string; choices: string[]; before: number[]; after: number[];
      actions: { piece: number; from: number; to: number; event: number }[] } };

export class Puzzle {
  readonly native: InstanceType<MainModule['Puzzle']>;
  readonly preview: ProgramPreview;
  constructor(module: MainModule, source: string) {
    const info = JSON.parse(module.kernelInfoJSON()) as KernelInfo;
    if (info.languageAPIVersion !== 1) throw new Error('Unsupported language API version.');
    try { this.native = new module.Puzzle(source); }
    catch (error: unknown) {
      const result = JSON.parse(module.compilePuzzleJSON(source)) as { diagnostics?: { message: string }[] };
      throw new Error(result.diagnostics?.map((d) => d.message).join('\n') ?? String(error));
    }
    this.preview = JSON.parse(this.native.inspectJSON()) as ProgramPreview;
  }
  query(query: ProgramQuery): ProgramResult { return JSON.parse(this.native.queryJSON(JSON.stringify(query))) as ProgramResult; }
  dispose(): void { this.native.delete(); }
}

export class KernelSession {
  readonly native: InstanceType<MainModule['Session']>;
  readonly definition: CompiledDefinition;
  readonly definitionText: string;

  private readonly module: MainModule;
  constructor(module: MainModule, source: string | KernelSession) {
    this.module = module;
    if (source instanceof KernelSession) {
      if (source.module !== module) throw new Error('A shared definition must belong to the same kernel runtime.');
      const shared = module.createSessionFromDefinition(source.native);
      if (!shared) throw new Error('Cannot create a session from the shared definition.');
      this.native = shared;
      this.definitionText = source.definitionText;
      this.definition = source.definition;
      return;
    }
    try {
      this.native = new module.Session(source);
    } catch (error: unknown) {
      // Recover structured diagnostics only for an invalid import; valid sources
      // compile once in Session, preserving exact native serialization.
      const result = JSON.parse(module.compileExecutionJSON(source)) as Result;
      throw new Error(result.diagnostics?.map((d) => `${d.source}: ${d.message}`).join('\n') ?? String(error));
    }
    this.definitionText = this.native.definitionJSON();
    this.definition = JSON.parse(this.definitionText) as CompiledDefinition;
  }

  snapshot(): Snapshot { return JSON.parse(this.native.snapshotJSON()) as Snapshot; }
  stateText(): string { return this.native.stateJSON(); }
  plan(request: Request, stateText = this.stateText()): Result {
    return JSON.parse(this.native.planJSON(JSON.stringify(request), stateText)) as Result;
  }
  validateState(stateText: string): Result {
    return JSON.parse(this.native.validateStateJSON(stateText)) as Result;
  }
  execute(request: Request, expectedRevision = this.native.revision()): Result {
    return JSON.parse(this.native.executeJSON(JSON.stringify(request), expectedRevision)) as Result;
  }
  move(operation: string, expectedRevision = this.native.revision()): Result {
    return this.execute({ operation, parameters: {} }, expectedRevision);
  }
  run(notation: string, policy: string, expectedRevision = this.native.revision()): Result {
    return JSON.parse(this.native.runJSON(notation, policy, expectedRevision)) as Result;
  }
  undo(expectedRevision = this.native.revision()): Result { return JSON.parse(this.native.undoJSON(expectedRevision)) as Result; }
  redo(expectedRevision = this.native.revision()): Result { return JSON.parse(this.native.redoJSON(expectedRevision)) as Result; }
  scramble(seed: number, length: number, expectedRevision = this.native.revision()): Result {
    return JSON.parse(this.native.scrambleJSON(seed, length, expectedRevision)) as Result;
  }
  load(document: string): Result { return JSON.parse(this.native.loadJSON(document)) as Result; }
  save(presentation: unknown = {}): string {
    return this.native.saveWithPresentationJSON(JSON.stringify(presentation));
  }
  dispose(): void { this.native.delete(); }
}

// Native memory views are borrowed. Every view is copied before the next WASM call.
export function copyFloatView(view: Float32Array): Float32Array { return new Float32Array(view); }
export function copyIndexView(view: Uint32Array): Uint32Array { return new Uint32Array(view); }
