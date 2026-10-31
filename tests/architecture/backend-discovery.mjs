// The Phase-1 oracle predates the explicitly additive Phase-7 discovery API.
// Validate that addition before comparing every original observation exactly.
import assert from 'node:assert/strict';

export function frozenKernelDiscovery(current, frozen) {
  const {geometryBackends:registry,...preserved}=current;
  assert.ok(registry,'Missing backend discovery extension');
  assert.equal(registry.registryVersion,1);
  assert.deepEqual(Object.keys(registry).sort(),['backends','capabilities','registryVersion']);
  const capabilities=[...new Set([...frozen.geometryCapabilities,'explicit-cube-tracks','uniform-cube-ports'])].sort();
  assert.deepEqual([...current.geometryCapabilities].sort(),capabilities,'Unexpected capability drift');
  assert.deepEqual(registry.capabilities,current.geometryCapabilities,'Registry capabilities disagree with kernel discovery');
  assert.deepEqual(registry.backends.filter(b=>b.id!=='artifact-cube-euclidean').map(b=>b.id),frozen.realizationKinds);
  assert.equal(new Set(registry.backends.map(b=>b.id)).size,registry.backends.length);
  assert.equal(registry.backends.filter(b=>b.id==='artifact-cube-euclidean').length,1);
  for(const backend of registry.backends) {
    assert.deepEqual(Object.keys(backend).sort(),['capabilities','envelopeVersions','id','payloadVersions','space','version']);
    assert.equal(backend.version,1);
    const versions=backend.id==='artifact-cube-euclidean'?[]:[1];
    assert.deepEqual(backend.envelopeVersions,versions);assert.deepEqual(backend.payloadVersions,versions);
    assert.ok(['R3','E2','S2','S3'].includes(backend.space));
    assert.ok(backend.capabilities.length>0);
    assert.equal(new Set(backend.capabilities).size,backend.capabilities.length);
    assert.ok(backend.capabilities.every(cap=>capabilities.includes(cap)));
  }
  // Only these two reviewed capability additions and their ordering differ.
  // API versions, definition formats and every other frozen field stay exact.
  return {...preserved,geometryCapabilities:frozen.geometryCapabilities};
}
