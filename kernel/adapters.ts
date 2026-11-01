// Shared host adapters. Type-only imports keep both distributions independent
// of the source compiler and of any particular WASM module factory.
import type { Request, TurnIntent, ControlCatalog } from './shared.js';

export interface SessionWire {
  snapshotJSON(): string; stateSnapshotJSON(): string; stateJSON(): string; revision(): string;
  lightweightResults(enabled: boolean): void;
  planJSON(request: string, state: string): string; validateStateJSON(state: string): string;
  executeJSON(request: string, revision: string): string;
  runJSON(notation: string, policy: string, revision: string): string;
  undoJSON(revision: string): string; redoJSON(revision: string): string;
  scrambleJSON(seed: number, length: number, revision: string): string;
  loadJSON(document: string): string; saveWithPresentationJSON(presentation: string): string;
  delete(): void;
}
export class SessionAdapter<Native extends SessionWire, Snapshot, StateSnapshot, Result> {
  constructor(readonly native: Native) {}
  snapshot(): Snapshot { return JSON.parse(this.native.snapshotJSON()) as Snapshot; }
  stateSnapshot(): StateSnapshot { return JSON.parse(this.native.stateSnapshotJSON()) as StateSnapshot; }
  lightweightResults(enabled = true): void { this.native.lightweightResults(enabled); }
  stateText(): string { return this.native.stateJSON(); }
  plan(request: Request, stateText = this.stateText()): Result { return JSON.parse(this.native.planJSON(JSON.stringify(request), stateText)) as Result; }
  validateState(text: string): Result { return JSON.parse(this.native.validateStateJSON(text)) as Result; }
  execute(request: Request, revision = this.native.revision()): Result { return JSON.parse(this.native.executeJSON(JSON.stringify(request), revision)) as Result; }
  move(operation: string, revision = this.native.revision()): Result { return this.execute({ operation, parameters: {} }, revision); }
  run(notation: string, policy: string, revision = this.native.revision()): Result { return JSON.parse(this.native.runJSON(notation, policy, revision)) as Result; }
  undo(revision = this.native.revision()): Result { return JSON.parse(this.native.undoJSON(revision)) as Result; }
  redo(revision = this.native.revision()): Result { return JSON.parse(this.native.redoJSON(revision)) as Result; }
  scramble(seed: number, length: number, revision = this.native.revision()): Result { return JSON.parse(this.native.scrambleJSON(seed, length, revision)) as Result; }
  load(text: string): Result { return JSON.parse(this.native.loadJSON(text)) as Result; }
  save(presentation: unknown = {}): string { return this.native.saveWithPresentationJSON(JSON.stringify(presentation)); }
  dispose(): void { this.native.delete(); }
}

export interface ControllerWire<Session> {
  catalogJSON(): string;
  familyStateJSON(session: Session, family: string): string;
  familiesForPiecesJSON(session: Session, pieces: string): string;
  destinationsJSON(session: Session, family: string, offset: number, limit: number): string;
  planToJSON(session: Session, intent: string, revision: string): string;
  turnToJSON(session: Session, intent: string, revision: string): string;
  delete(): void;
}
export class ControllerAdapter<Session extends Pick<SessionWire, 'revision'>,
  Native extends ControllerWire<Session>, Result, Selection> {
  readonly controls: ControlCatalog;
  constructor(readonly native: Native, protected readonly session: { readonly native: Session }) {
    this.controls = JSON.parse(native.catalogJSON()) as ControlCatalog;
  }
  catalog(): ControlCatalog { return this.controls; }
  familyState(family: string): Result { return JSON.parse(this.native.familyStateJSON(this.session.native, family)) as Result; }
  familiesForPieces(pieces: readonly string[]): Selection { return JSON.parse(this.native.familiesForPiecesJSON(this.session.native, JSON.stringify(pieces))) as Selection; }
  destinations(family: string, offset = 0, limit = 16): Result { return JSON.parse(this.native.destinationsJSON(this.session.native, family, offset, limit)) as Result; }
  planTo(intent: TurnIntent, revision = this.session.native.revision()): Result { return JSON.parse(this.native.planToJSON(this.session.native, JSON.stringify(intent), revision)) as Result; }
  turnTo(intent: TurnIntent, revision = this.session.native.revision()): Result { return JSON.parse(this.native.turnToJSON(this.session.native, JSON.stringify(intent), revision)) as Result; }
  dispose(): void { this.native.delete(); }
}
