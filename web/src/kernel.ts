import createModule, { type MainModule } from '../generated/twisty.mjs';
import wasmUrl from '../generated/twisty.wasm?url';

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
  implicatedPieces?: string[]; evidence?: unknown; failureIndex?: number;
  diagnostics?: { reasonCode: string; source: string; message: string }[];
  scramble?: { seed: number; generatorVersion: string; notation: string; requests: string[]; termination: string };
};
export type CompiledDefinition = {
  kind: string; puzzleId: string; definitionDigest: string;
  pieceTypes: { id: string; placementDomainId: string; localPorts: string[] }[];
  placementDomains: { id: string; placements: { key: string; footprint: string[]; portAttachment: Record<string, { cell: string; attachment: string }> }[] }[];
  pieces: { id: string; type: string; homePlacement: string; portLabels: Record<string, string> }[];
  operations: { id: string; inverse: string; family: string; selectedCells?: string[]; transport: string }[];
  symmetry?: { members?: number[][]; positionOrbits?: Record<string, { representative: string; stabilizer: number[][]; positions: { id: string }[] }> }; cells: string[]; provenance: unknown;
};
export type Realization = {
  schemaVersion: number; id: string; kind: 'cube-euclidean' | 'cube-port-diagram';
  compatibleDefinitionDigest: string; requiredCapabilities: string[];
};

let runtime: Promise<MainModule> | undefined;
export function loadRuntime(): Promise<MainModule> {
  runtime ??= createModule({ locateFile: (filename: string) => filename.endsWith('.wasm') ? wasmUrl : filename });
  return runtime;
}

export class KernelSession {
  readonly native: InstanceType<MainModule['Session']>;
  readonly definition: CompiledDefinition;
  readonly definitionText: string;

  constructor(module: MainModule, source: string) {
    const compiled = JSON.parse(module.compileJSON(source)) as Result & { definition?: CompiledDefinition };
    if (compiled.status !== 'Compiled' || !compiled.definition) {
      throw new Error(compiled.diagnostics?.map((d) => `${d.source}: ${d.message}`).join('\n') ?? 'Invalid definition.');
    }
    this.native = new module.Session(source);
    this.definitionText = this.native.definitionJSON();
    this.definition = JSON.parse(this.definitionText) as CompiledDefinition;
  }

  snapshot(): Snapshot { return JSON.parse(this.native.snapshotJSON()) as Snapshot; }
  stateText(): string { return this.native.stateJSON(); }
  move(operation: string): Result {
    return JSON.parse(this.native.executeJSON(JSON.stringify({ operation, parameters: {} }), this.snapshot().revision)) as Result;
  }
  run(notation: string, policy: string): Result {
    return JSON.parse(this.native.runJSON(notation, policy, this.snapshot().revision)) as Result;
  }
  undo(): Result { return JSON.parse(this.native.undoJSON(this.snapshot().revision)) as Result; }
  redo(): Result { return JSON.parse(this.native.redoJSON(this.snapshot().revision)) as Result; }
  scramble(seed: number, length: number): Result {
    return JSON.parse(this.native.scrambleJSON(seed, length, this.snapshot().revision)) as Result;
  }
  load(document: string): Result { return JSON.parse(this.native.loadJSON(document)) as Result; }
  save(presentation: unknown): string {
    return this.native.saveWithPresentationJSON(JSON.stringify(presentation));
  }
  dispose(): void { this.native.delete(); }
}

// Native memory views are borrowed. Every view is copied before the next WASM call.
export function copyFloatView(view: Float32Array): Float32Array { return new Float32Array(view); }
export function copyIndexView(view: Uint32Array): Uint32Array { return new Uint32Array(view); }
