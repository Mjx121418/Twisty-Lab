import { expect, it } from 'vitest';
// @ts-expect-error independent Node conformance helper has no TS declaration
import { frozenKernelDiscovery } from '../architecture/backend-discovery.mjs';

const frozen={apiVersion:7,geometryCapabilities:['triangle-meshes','rigid-transforms'],realizationKinds:['cube-euclidean']};
function fixture() {
  const capabilities=[...frozen.geometryCapabilities,'explicit-cube-tracks','uniform-cube-ports'].sort();
  const backend=(id:string,versions:number[])=>({id,version:1,envelopeVersions:versions,payloadVersions:versions,
    capabilities:['triangle-meshes','rigid-transforms'],space:'R3'});
  return {...frozen,geometryCapabilities:capabilities,geometryBackends:{registryVersion:1,capabilities,
    backends:[backend('artifact-cube-euclidean',[]),backend('cube-euclidean',[1])]}};
}
it('compares the frozen kernel metadata after validating the explicit discovery extension',()=>{
  expect(frozenKernelDiscovery(fixture(),frozen)).toEqual(frozen);
});
it('rejects missing old capabilities, unknown additions and unsupported backend versions',()=>{
  const missing=fixture();missing.geometryCapabilities=missing.geometryCapabilities.filter(cap=>cap!=='rigid-transforms');
  expect(()=>frozenKernelDiscovery(missing,frozen)).toThrow();
  const extra=fixture();extra.geometryCapabilities.push('unreviewed');
  expect(()=>frozenKernelDiscovery(extra,frozen)).toThrow();
  const version=fixture();version.geometryBackends.backends[1].version=2;
  expect(()=>frozenKernelDiscovery(version,frozen)).toThrow();
});
it('preserves drift in other frozen fields so the strict baseline comparison rejects it',()=>{
  const changed=fixture();changed.apiVersion=8;
  expect(frozenKernelDiscovery(changed,frozen)).not.toEqual(frozen);
});
