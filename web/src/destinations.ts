import type { KernelSession, Request, Snapshot } from './kernel';

export type PieceDestination = {
  pieceId: string;
  request: Request;
  revision: string;
  definitionDigest: string;
  transitionRecord: string;
};

// Plan against one exact source state. Geometry consumes the original records;
// parsed placements are only used to identify participating pieces.
export function pieceDestinations(session: KernelSession, snapshot: Snapshot, pieceId: string): PieceDestination[] {
  if (session.native.revision() !== snapshot.revision || session.definition.definitionDigest !== snapshot.definitionDigest) return [];
  const source = session.stateText();
  const destinations: PieceDestination[] = [];
  for (const request of snapshot.legalRequests) {
    const plan = session.plan(request, source);
    if (plan.status !== 'Legal' || !plan.transitionRecord || !plan.transition?.pieceActions.some((action) => action.pieceId === pieceId)) continue;
    // A participating center can keep its placement while its layer twists.
    destinations.push({ pieceId, request, revision: snapshot.revision, definitionDigest: snapshot.definitionDigest, transitionRecord: plan.transitionRecord });
  }
  return destinations;
}
