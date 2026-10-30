import * as THREE from 'three';

export type S3Mesh = THREE.Mesh<THREE.BufferGeometry, THREE.MeshStandardMaterial | THREE.MeshStandardMaterial[]>;

/** Keep the transparent back/front passes on one sortable object, with stable
 * material configurations instead of Three.js changing sides every draw. */
export class S3MaterialPasses {
  private readonly pairs = new Map<THREE.MeshStandardMaterial, THREE.MeshStandardMaterial[]>();

  sync(mesh: S3Mesh, source: THREE.MeshStandardMaterial): void {
    if (!source.transparent || !mesh.visible) { mesh.material = source; return; }
    let pair = this.pairs.get(source);
    if (!pair) {
      pair = [THREE.BackSide, THREE.FrontSide].map(side => {
        const material = source.clone();
        material.side = side;
        // Material.clone() does not retain these callbacks. Both passes share
        // the source's small pose/fade uniform objects, not copies of their values.
        material.onBeforeCompile = source.onBeforeCompile;
        material.customProgramCacheKey = source.customProgramCacheKey;
        return material;
      });
      this.pairs.set(source, pair);
    }
    for (const material of pair) {
      material.color.copy(source.color);
      material.emissive.copy(source.emissive);
      material.emissiveIntensity = source.emissiveIntensity;
      material.opacity = source.opacity;
      material.depthWrite = source.depthWrite;
      material.depthTest = source.depthTest;
    }
    const geometry = mesh.geometry;
    const count = geometry.index?.count ?? geometry.getAttribute('position').count;
    const groups = geometry.groups;
    if (groups.length !== 2 || groups[0].count !== count || groups[1].count !== count) {
      geometry.clearGroups();
      geometry.addGroup(0, count, 0);
      geometry.addGroup(0, count, 1);
    }
    // Equal sort keys retain group order: back then front for this same object,
    // including ties with other surfaces. Opaque single materials ignore groups.
    mesh.material = pair;
  }

  dispose(): void {
    for (const pair of this.pairs.values()) for (const material of pair) material.dispose();
    this.pairs.clear();
  }
}
