export type ViewId = 'cube' | 'sphere' | 'diagram' | 'fundamental-domain' | 'tiled-plane';
export type ViewSelection = { order: ViewId[]; visible: ViewId[] };

export const viewLabels: Record<ViewId, string> = {
  cube: 'Cube', sphere: 'Sphere', diagram: 'Port diagram',
  'fundamental-domain': 'Fundamental domain', 'tiled-plane': 'Tiled plane',
};

export function defaultViews(available: readonly ViewId[]): ViewSelection {
  return { order: [...available], visible: available.filter(id => id !== 'diagram' || !available.includes('sphere')) };
}

function knownViews(value: unknown, available: readonly ViewId[]): ViewId[] {
  return Array.isArray(value) ? [...new Set(value.filter((id): id is ViewId => available.includes(id)))] : [];
}

/** Presentation only. Older pair/layout saves remain readable. */
export function restoreViews(presentation: unknown, available: readonly ViewId[]): { selection: ViewSelection; cameraOrder: (ViewId | undefined)[] } {
  const saved = presentation as { viewOrder?: unknown; visibleViews?: unknown; viewPair?: string; layout?: string } | undefined;
  const defaults = defaultViews(available);
  if (!saved) return { selection: defaults, cameraOrder: defaults.order };
  if (Array.isArray(saved.viewOrder) && Array.isArray(saved.visibleViews)) {
    const order = knownViews(saved.viewOrder, available);
    order.push(...available.filter(id => !order.includes(id)));
    const visible = knownViews(saved.visibleViews, available);
    const seen = new Set<ViewId>();
    const cameraOrder = saved.viewOrder.map(id => {
      if (!available.includes(id) || seen.has(id)) return undefined;
      seen.add(id); return id as ViewId;
    });
    return { selection: { order, visible: visible.length ? visible : defaults.visible }, cameraOrder };
  }
  const pairs: Record<string, ViewId[]> = {
    'cube-sphere': ['cube', 'sphere'], 'cube-diagram': ['cube', 'diagram'],
    'sphere-diagram': ['sphere', 'diagram'], 'square-tiles': ['fundamental-domain', 'tiled-plane'],
  };
  const pair = Object.hasOwn(pairs, saved.viewPair ?? '') ? pairs[saved.viewPair!].filter(id => available.includes(id)) : undefined;
  const cameraOrder = pair?.length ? pair : defaults.visible;
  const visible = saved.layout === 'cube' ? cameraOrder.slice(0, 1) : saved.layout === 'diagram' ? cameraOrder.slice(1, 2) : cameraOrder;
  return { selection: { order: [...cameraOrder, ...available.filter(id => !cameraOrder.includes(id))], visible: visible.length ? visible : defaults.visible }, cameraOrder };
}

export function moveView(selection: ViewSelection, id: ViewId, direction: -1 | 1): ViewSelection {
  const order = [...selection.order], from = order.indexOf(id), to = from + direction;
  if (from < 0 || to < 0 || to >= order.length) return selection;
  [order[from], order[to]] = [order[to], order[from]];
  return { ...selection, order };
}
