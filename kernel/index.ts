import { SessionAdapter, ControllerAdapter } from './adapters.js';
import createModule, { type MainModule } from './generated/twisty.mjs';
export type { MainModule } from './generated/twisty.mjs';
import { type CompiledDefinition, type Snapshot, type StateSnapshot, type Result,
  type ControlResult, type FamilySelection } from './shared.js';
export * from './shared.js';


export type KernelInfo = {
  apiVersion: number; version: string; documentKinds: string[]; frameBufferVersion: number;
  languageAPIVersion: number;
  controllerAPIVersion: number; controllerCapabilities: string[];
  geometryAPIVersion: number; poseBufferVersion: number;
  intrinsicMeshBufferVersion?: number;
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
  if (info.apiVersion !== 7 || info.frameBufferVersion !== 1) throw new Error('Unsupported kernel API or frame-buffer version.');
  return module;
}

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
export type CompactProgram = {
  groups?: { order: number; products: number[]; inverses: number[] }[];
  actions?: { group: number; size: number; kind: 'regular' | 'tabulated' | 'trivial'; table?: number[] }[];
  collections?: { id?: string; action: number; placements: number[] }[];
  spaces?: { size: number }[];
  /** Legacy format-2 square maps. Format 3 uses positionMaps below. */
  permutations?: { space: number; rows?: [number, number][]; base?: number;
    inputAction?: number; inputElement?: number; outputAction?: number; outputElement?: number }[];
  bindings?: { space: number; placements?: number[]; base?: number; group?: number; element?: number }[];
  positionMaps?: { sourceSpace: number; targetSpace: number; rows?: [number, number][]; base?: number;
    inputAction?: number; inputElement?: number; outputAction?: number; outputElement?: number }[];
  families?: ({ space: number; positions: number[] } | { anchorSpace: number; referenceSpace: number; positions: number[] })[];
  maps?: ({ kind: 'explicit'; rows: [number, number][] }
    | { kind: 'group'; collection: number; element: number; support: number[] }
    | { kind: 'difference'; family: number; binding: number; source: number; target: number }
    | { kind: 'union'; parts: number[] })[];
};
export type PuzzleSource = {
  kind: 'relational-puzzle'; languageVersion: 1 | 2 | 3; id: string; compact?: CompactProgram;
  domains: {
    id: string; placements: string[]; pieces: { id: string; initial: string }[];
    moves: { id: string; support?: string[]; transport?: Record<string, string>; map?: number;
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
  kind: 'relational-package'; formatVersion: 1 | 2 | 3;
  source: PuzzleSource; catalog: CubeCatalog | PortCatalog | ArticulatedCatalog;
};
export type PuzzleDefinition = CompiledDefinition & {
  kind: 'relational-definition'; formatVersion: 1 | 2 | 3; predicateVersion: 1;
  source: PuzzleSource; catalog: CubeCatalog | PortCatalog | ArticulatedCatalog;
  program: ProgramPreview['program']; bindings: ProgramPreview['bindings'];
};
export function createCubeSource(module: MainModule, bandaged = false): {
  source: PuzzleSource; catalog: CubeCatalog;
} {
  return JSON.parse(module.cubeModuleJSON(bandaged)) as { source: PuzzleSource; catalog: CubeCatalog };
}
export type ProgramPreview = {
  kind: 'relational-preview'; experimentalVersion: 1 | 2 | 3; predicateVersion: 1;
  bindings: {
    puzzleId: string; pieces: string[]; placements: string[]; events: string[];
    choiceSlots: string[]; choiceValues: string[][]; moveMaps?: number[][]; collections?: string[];
    requests: { id: string; profiles: { id: string; choices: string[]; whenRoot: number }[]; defaultProfile: string | null }[];
  };
  program: { pieceCount: number; placementCount: number; initialPlacements: number[];
    finiteSets: number[][]; predicateNodes: Record<string, unknown>[]; childReferences: number[];
    transportMaps?: [number, number][][]; compact?: CompactProgram; transportStorageBytes?: number; actionRecords: { piece: number; map: number; selectionRoot: number; event: number }[];
    requestPrograms: { choiceSizes: number[]; firstAction: number; actionCount: number; relationRoot: number }[];
    invariantRoot: number; goalRoot: number };
};
export type ProgramQuery = { op: 'act'; collection: number; element: number; placement: number }
  | { op: 'transport'; map: number; placement: number }
  | { op: 'positionMap'; map: number; point: number; inverse?: boolean }
  | ({ state: number[] } & (
  { op: 'validate' | 'goal' }
  | { op: 'plan'; request: string; choices: string[] }
  | { op: 'bind'; request: string; profile?: string }
  | { op: 'discover'; request: string; candidateLimit: number; resultLimit: number }));
export type ProgramResult =
  | { status: 'Invalid'; diagnostics?: { code: string; message: string }[] }
  | { status: 'Valid' | 'ChoiceRequired' }
  | { status: 'Evaluated'; solved: boolean }
  | { status: 'Evaluated'; placement: number | null; participates?: boolean }
  | { status: 'Evaluated'; point: number | null; defined: boolean }
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
    if (info.languageAPIVersion !== 3) throw new Error('Unsupported language API version.');
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

export class KernelSession extends SessionAdapter<InstanceType<MainModule['Session']>, Snapshot, StateSnapshot, Result> {
  readonly definition: CompiledDefinition;
  readonly definitionText: string;
  private readonly module: MainModule;
  constructor(module: MainModule, source: string | KernelSession) {
    let native: InstanceType<MainModule['Session']>, definitionText: string, definition: CompiledDefinition;
    if (source instanceof KernelSession) {
      if (source.module !== module) throw new Error('A shared definition must belong to the same kernel runtime.');
      const shared = module.createSessionFromDefinition(source.native);
      if (!shared) throw new Error('Cannot create a session from the shared definition.');
      native = shared; definitionText = source.definitionText; definition = source.definition;
    } else {
      try { native = new module.Session(source); }
      catch (error: unknown) {
        const result = JSON.parse(module.compileExecutionJSON(source)) as Result;
        throw new Error(result.diagnostics?.map(d => `${d.source}: ${d.message}`).join('\n') ?? String(error));
      }
      definitionText = native.definitionJSON(); definition = JSON.parse(definitionText) as CompiledDefinition;
    }
    super(native);
    this.module = module; this.definitionText = definitionText; this.definition = definition;
  }
}

export class KernelController extends ControllerAdapter<InstanceType<MainModule['Session']>,
  NonNullable<ReturnType<MainModule['createController']>>, ControlResult, FamilySelection> {
  constructor(module: MainModule, session: KernelSession) {
    const native = module.createController(session.native);
    if (!native) throw new Error('Cannot create controller.');
    super(native, session);
  }
  override familiesForPieces(pieces: readonly string[]): FamilySelection {
    if (typeof this.native.familiesForPiecesJSON !== 'function')
      return { status: 'Unavailable', reasonCode: 'controls.piece_selection_unsupported' };
    return super.familiesForPieces(pieces);
  }
}
