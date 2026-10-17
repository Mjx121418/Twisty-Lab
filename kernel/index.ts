import createModule, { type MainModule } from './generated/twisty.mjs';
export type { MainModule } from './generated/twisty.mjs';

export type KernelInfo = {
  apiVersion: number; version: string; documentSchemaVersion: number; frameBufferVersion: number;
  experimentalAPIs?: { relational?: number };
  relationalLibraries?: string[];
  relationalDefinitionVersion?: number; sessionSchemaVersions?: number[];
  ruleModules: string[]; realizationKinds: string[]; geometryCapabilities: string[];
};
export type KernelOptions = { wasmURL?: string | URL; locateFile?: (filename: string, directory: string) => string };
export async function createKernel(options: KernelOptions = {}): Promise<MainModule> {
  const locateFile = options.locateFile ?? ((filename: string, directory: string) =>
    filename.endsWith('.wasm') && options.wasmURL ? String(options.wasmURL) : directory + filename);
  const module = await createModule({ locateFile });
  const info = JSON.parse(module.kernelInfoJSON()) as KernelInfo;
  if (info.apiVersion !== 2 || info.frameBufferVersion !== 1) throw new Error('Unsupported kernel API or frame-buffer version.');
  return module;
}

export type State = { placementOf: Record<string, string>; mechanism: Record<string, unknown> };
export type Request = { operation: string; parameters: Record<string, never>; choices?: string[] };
export type Snapshot = {
  puzzleId: string; definitionDigest: string; stateDigest: string; revision: string;
  state: State; solved: boolean; legalRequests: Request[];
  history: string[]; cursor: number; canUndo: boolean; canRedo: boolean;
};
export type Transition = {
  definitionDigest: string; beforeStateDigest: string; afterStateDigest: string;
  request: Request; beforeState: State; afterState: State;
  witnessVersion?: number;
  pieceActions: { pieceId: string; from: string; to: string; role: string; transport: string; event?: string }[];
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
  ambientSpace?: 'R3' | 'S2'; sphereRadius?: number; diskAngleDegrees?: number; fidelity?: string;
  diskCenters?: Record<string, [number, number, number]>; surfaceTransportVerified?: boolean; catalogTransportVerified?: boolean;
};
export type HitBinding = {
  status: string; pieceId?: string; portId?: string; operationCandidates?: Request[];
};
export type CompiledDefinition = {
  kind: string; puzzleId: string; definitionDigest: string;
  ruleModule: 'relational@1' | 'finite-footprint@1' | 'finite-placement-relations@1' | 'finite-placement-relations@2';
  pieceTypes: { id: string; placementDomainId: string; localPorts: string[] }[];
  placementDomains: { id: string; placements: { key: string; footprint: string[]; portAttachment: Record<string, { cell: string; attachment: string }> }[] }[];
  pieces: { id: string; type: string; homePlacement: string; portLabels: Record<string, string> }[];
  operations: { id: string; inverse: string; family: string; selectedCells?: string[]; transport: string; pieceGuards?: Record<string, string>; placementCountGuards?: PlacementCountGuard[] }[];
  symmetry?: { members?: number[][]; positionOrbits?: Record<string, { representative: string; stabilizer: number[][]; positions: { id: string }[] }> }; cells: string[]; provenance: unknown;
};
export type PlacementCountGuard = {
  id: string; placementDomainId: string; placements: string[]; min: number; max: number;
};
export type PolyhedralModel = {
  vertices: number[][]; ports: Record<string, number[]>; symmetries: number[][];
  surfaceVertices?: number[][];
};
export type Realization = {
  schemaVersion: number; id: string; kind: 'cube-euclidean' | 'cube-port-diagram' | 'cube-spherical' | 'polyhedral-euclidean' | 'polyhedral-port-diagram' | 'polyhedral-spherical' | 'articulated-euclidean' | 'articulated-port-diagram' | 'articulated-spherical';
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
export type GearCubeSource = {
  schemaVersion: 1; semanticsVersion: string; kind: 'gear-cube-prototypes'; puzzleId: string;
  gearedEdges: string[];
};

// Experimental version 1. Numeric placements are resolved through preview.bindings.
// This pure preview/query API remains separate from persisted session packages.
export type RelationalPredicate = boolean
  | { at: { piece: string; placements: string[] } }
  | { chosen: { domain: string; moves: string[] } }
  | { request: string[] } | { ref: string } | { not: RelationalPredicate }
  | { all: RelationalPredicate[] } | { any: RelationalPredicate[] }
  | { implies: [RelationalPredicate, RelationalPredicate] } | { iff: [RelationalPredicate, RelationalPredicate] }
  | { count: { min: number; max: number; of: RelationalPredicate[] } }
  | { occupiedBy: { placement: string; pieces: string[] } };
export type RelationalSource = {
  kind: 'relational-puzzle'; languageVersion: 1; id: string;
  domains: {
    id: string; placements: string[]; pieces: { id: string; initial: string }[];
    moves: { id: string; support: string[]; transport: Record<string, string>;
      pieces?: string[]; requires?: RelationalPredicate; event?: string }[];
  }[];
  predicates?: Record<string, RelationalPredicate>;
  relations?: { id: string; predicate: RelationalPredicate }[];
  invariant?: RelationalPredicate; goal?: RelationalPredicate;
  requests: { id: string; relation: RelationalPredicate;
    profiles?: { id: string; choices: Record<string, string>; when?: RelationalPredicate }[]; defaultProfile?: string }[];
};
export type RelationalCubeCatalog = {
  kind: 'relational-cube-bindings'; version: 1; puzzleId: string;
  faceLabels: string[]; symmetry: number[][]; resources: string[];
  domains: { id: string; pieces: string[]; placements: string[]; localPorts: string[] }[];
  pieces: Record<string, { domain: string; home: string; portLabels: Record<string, string> }>;
  placements: Record<string, { resources: string[]; ports: Record<string, { cell: string; attachment: string }> }>;
  requests: Record<string, { inverse: string; family: string; event: string; profile: string }>;
};
export type RelationalPackage = {
  kind: 'relational-package'; formatVersion: 1;
  source: RelationalSource; catalog: RelationalCubeCatalog;
};
export type RelationalDefinition = CompiledDefinition & {
  kind: 'relational-definition'; formatVersion: 1; predicateVersion: 1;
  source: RelationalSource; catalog: RelationalCubeCatalog;
  program: RelationalPreview['program']; bindings: RelationalPreview['bindings'];
};
export function createRelationalCubeSource(module: MainModule, bandaged = false): {
  source: RelationalSource; catalog: RelationalCubeCatalog;
} {
  return JSON.parse(module.relationalCubeModuleJSON(bandaged)) as { source: RelationalSource; catalog: RelationalCubeCatalog };
}
export type RelationalPreview = {
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
export type RelationalQuery = { state: number[] } & (
  { op: 'validate' | 'goal' }
  | { op: 'plan'; request: string; choices: string[] }
  | { op: 'bind'; request: string; profile?: string }
  | { op: 'discover'; request: string; candidateLimit: number; resultLimit: number });
export type RelationalResult =
  | { status: 'Invalid'; diagnostics?: { code: string; message: string }[] }
  | { status: 'Valid' | 'ChoiceRequired' }
  | { status: 'Evaluated'; solved: boolean }
  | { status: 'Selected'; choices: string[] }
  | { status: 'Discovered'; choices: string[][]; examined: number; complete: boolean }
  | { status: 'Rejected'; reason: 'relation' | 'conflicting-writes' | 'target-invariant' }
  | { status: 'Permitted'; transition: { request: string; choices: string[]; before: number[]; after: number[];
      actions: { piece: number; from: number; to: number; event: number }[] } };

export class ExperimentalRelationalPuzzle {
  readonly native: InstanceType<MainModule['RelationalPuzzle']>;
  readonly preview: RelationalPreview;
  constructor(module: MainModule, source: string) {
    const info = JSON.parse(module.kernelInfoJSON()) as KernelInfo;
    if (info.experimentalAPIs?.relational !== 1) throw new Error('Unsupported relational API version.');
    try { this.native = new module.RelationalPuzzle(source); }
    catch (error: unknown) {
      const result = JSON.parse(module.compileRelationalJSON(source)) as { diagnostics?: { message: string }[] };
      throw new Error(result.diagnostics?.map((d) => d.message).join('\n') ?? String(error));
    }
    this.preview = JSON.parse(this.native.inspectJSON()) as RelationalPreview;
  }
  query(query: RelationalQuery): RelationalResult { return JSON.parse(this.native.queryJSON(JSON.stringify(query))) as RelationalResult; }
  dispose(): void { this.native.delete(); }
}

export class KernelSession {
  readonly native: InstanceType<MainModule['Session']>;
  readonly definition: CompiledDefinition;
  readonly definitionText: string;

  constructor(module: MainModule, source: string) {
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
  convertLegacy(document: string, legacyDefinition: string): { status: string; session?: unknown; diagnostics?: Result['diagnostics'] } {
    return JSON.parse(this.native.convertLegacyJSON(document, legacyDefinition));
  }
  save(presentation: unknown = {}): string {
    return this.native.saveWithPresentationJSON(JSON.stringify(presentation));
  }
  dispose(): void { this.native.delete(); }
}

// Native memory views are borrowed. Every view is copied before the next WASM call.
export function copyFloatView(view: Float32Array): Float32Array { return new Float32Array(view); }
export function copyIndexView(view: Uint32Array): Uint32Array { return new Uint32Array(view); }
