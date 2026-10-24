import { copyFloatView, type GeometryInstance, type KernelSession, type Request, type Snapshot } from '../../kernel/index';

export type PieceDestination = {
  pieceId: string;
  request: Request;
  revision: string;
  definitionDigest: string;
  transitionRecord: string;
  targetStateText: string;
};

const planned = new WeakMap<KernelSession, {
  revision: string;
  definitionDigest: string;
  pieces: Map<string, PieceDestination[]>;
}>();

// Plan against one exact source state. Retain the original witness for
// execution, and project its verified target through the kernel frame API.
export function pieceDestinations(session: KernelSession, snapshot: Snapshot, pieceId: string): PieceDestination[] {
  if (session.native.revision() !== snapshot.revision || session.definition.definitionDigest !== snapshot.definitionDigest) return [];
  if (!session.definition.pieces.some(piece => piece.id === pieceId)) return [];
  const previous = planned.get(session);
  if (previous?.revision === snapshot.revision && previous.definitionDigest === snapshot.definitionDigest)
    return previous.pieces.get(pieceId)?.slice() ?? [];
  const source = session.stateText();
  const pieces = new Map<string, PieceDestination[]>();
  for (const request of snapshot.legalRequests) {
    const plan = session.plan(request, source);
    if (plan.status !== 'Legal' || !plan.transitionRecord || !plan.transition) continue;
    const targetStateText = JSON.stringify(plan.transition.afterState);
    // A participating center can keep its placement while its layer twists.
    // One verified plan serves every participant's selection at this revision.
    for (const action of plan.transition.pieceActions) {
      const destinations = pieces.get(action.pieceId) ?? [];
      destinations.push({ pieceId: action.pieceId, request, revision: snapshot.revision,
        definitionDigest: snapshot.definitionDigest, transitionRecord: plan.transitionRecord, targetStateText });
      pieces.set(action.pieceId, destinations);
    }
  }
  planned.set(session, { revision: snapshot.revision, definitionDigest: snapshot.definitionDigest, pieces });
  return pieces.get(pieceId)?.slice() ?? [];
}

// Endpoint ghosts do not need an animation. The plan already verified the
// request; the frame API checks the target state and preserves live motion.
export function destinationTransforms(geometry: GeometryInstance, destination: PieceDestination): Float32Array {
  if (geometry.scene.definitionDigest !== destination.definitionDigest)
    throw new Error('Destination belongs to a different definition.');
  const frame = geometry.definition.frame(destination.targetStateText);
  try {
    const drawing = geometry.definition.project(frame, 'embedding');
    try { return copyFloatView(drawing.transforms() as Float32Array); }
    finally { drawing.delete(); }
  } finally { frame.delete(); }
}
