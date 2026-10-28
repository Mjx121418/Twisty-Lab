export type S3FadeSettings = { minRadius: number; maxStartRadius: number; maxEndRadius: number; endRadius: number; startRadius: number };

/** Radii are measured after stereographic projection, in units of its scale. */
export function s3FadeRange(cutDepth: number, centers: readonly number[][]) {
  const alignments = centers.slice(1).map(c => c.reduce((dot, v, j) => dot + v * centers[0][j], 0));
  const adjacentAlignment = Math.max(...alignments);
  const distantAlignment = Math.max(...alignments.filter(dot => dot < adjacentAlignment - 1e-8));
  // The focused cutting ball ends at W=-cutDepth. A neighboring cutting ball
  // reaches at most this W, so no visible piece needs to extend farther.
  const endW = -adjacentAlignment * cutDepth + Math.sqrt((1 - adjacentAlignment ** 2) * (1 - cutDepth ** 2));
  const neighborRadius = Math.sqrt((1 + endW) / (1 - endW));
  const endRadius = Math.ceil(100 * neighborRadius) / 100;
  const minRadius = Math.ceil(100 * Math.sqrt((1 - cutDepth) / (1 + cutDepth))) / 100;
  const maxStartRadius = Math.floor(100 * .95 * neighborRadius) / 100;
  return { minRadius, maxStartRadius, endRadius, adjacentAlignment, distantAlignment };
}

/** Smooth vertex fade with an opaque inner ball and a finite outer cutoff. */
export function s3Opacity(w: number, startRadius: number, endRadius: number): number {
  if (w >= 1) return 0;
  const radius = Math.sqrt(Math.max(0, (1 + w) / (1 - w)));
  const t = Math.max(0, Math.min(1, (radius - startRadius) / (endRadius - startRadius)));
  return 1 - t * t * (3 - 2 * t);
}

/** At a settled pose, a piece belongs if any active cut is centered on the
 * focused cell or a face neighbor. Interpolate between rings during turns. */
export function s3CellVisibility(alignment: number, distantAlignment: number, adjacentAlignment: number): number {
  const t = Math.max(0, Math.min(1, (alignment - distantAlignment) / (adjacentAlignment - distantAlignment)));
  // Exact dyadic frames can acquire tiny rounding errors after focus rotation.
  if (t < 1e-8) return 0;
  if (t > 1 - 1e-8) return 1;
  return t * t * (3 - 2 * t);
}
