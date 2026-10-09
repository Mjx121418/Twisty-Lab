import createModule, { type MainModule } from './generated/twisty.mjs';
export type { MainModule } from './generated/twisty.mjs';

export type KernelInfo = {
  apiVersion: number; version: string; documentSchemaVersion: number; frameBufferVersion: number;
  ruleModules: string[]; realizationKinds: string[]; geometryCapabilities: string[];
};
export type KernelOptions = { wasmURL?: string | URL; locateFile?: (filename: string, directory: string) => string };
export async function createKernel(options: KernelOptions = {}): Promise<MainModule> {
  const locateFile = options.locateFile ?? ((filename: string, directory: string) =>
    filename.endsWith('.wasm') && options.wasmURL ? String(options.wasmURL) : directory + filename);
  const module = await createModule({ locateFile });
  const info = JSON.parse(module.kernelInfoJSON()) as KernelInfo;
  if (info.apiVersion !== 1 || info.frameBufferVersion !== 1) throw new Error('Unsupported kernel API or frame-buffer version.');
  return module;
}

export type State = { placementOf: Record<string, string>; mechanism: Record<string, unknown> };
export type Request = { operation: string; parameters: Record<string, never> };
export type Snapshot = {
  puzzleId: string; definitionDigest: string; stateDigest: string; revision: string;
  state: State; solved: boolean; legalRequests: Request[];
  history: string[]; cursor: number; canUndo: boolean; canRedo: boolean;
};
export type Transition = {
  definitionDigest: string; beforeStateDigest: string; afterStateDigest: string;
  request: Request; beforeState: State; afterState: State;
  pieceActions: { pieceId: string; from: string; to: string; role: string; transport: string }[];
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
  diskCenters?: Record<string, [number, number, number]>; surfaceTransportVerified?: boolean;
};
export type HitBinding = {
  status: string; pieceId?: string; portId?: string; operationCandidates?: Request[];
};
export type CompiledDefinition = {
  kind: string; puzzleId: string; definitionDigest: string;
  pieceTypes: { id: string; placementDomainId: string; localPorts: string[] }[];
  placementDomains: { id: string; placements: { key: string; footprint: string[]; portAttachment: Record<string, { cell: string; attachment: string }> }[] }[];
  pieces: { id: string; type: string; homePlacement: string; portLabels: Record<string, string> }[];
  operations: { id: string; inverse: string; family: string; selectedCells?: string[]; transport: string; pieceGuards?: Record<string, string> }[];
  symmetry?: { members?: number[][]; positionOrbits?: Record<string, { representative: string; stabilizer: number[][]; positions: { id: string }[] }> }; cells: string[]; provenance: unknown;
};
export type Realization = {
  schemaVersion: number; id: string; kind: 'cube-euclidean' | 'cube-port-diagram' | 'cube-spherical' | 'polyhedral-euclidean' | 'polyhedral-port-diagram';
  compatibleDefinitionDigest: string; requiredCapabilities: string[];
};

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
      const result = JSON.parse(module.compileJSON(source)) as Result;
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
