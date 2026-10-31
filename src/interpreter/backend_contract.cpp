#include "twisty/contracts.hpp"
#include "backend_contract.hpp"
#include <algorithm>
#include <set>

namespace twisty::detail {
namespace {
constexpr const char *spatial_caps[]{"triangle-meshes", "rigid-transforms", "quotient-surface",
  "quotient-orbifold", "shared-rotation-tracks", "family-rotation-generators",
  "transport-path-selectors", "explicit-cube-tracks"};
constexpr const char *legacy_cube_caps[]{"triangle-meshes", "rigid-transforms", "explicit-cube-tracks"};
constexpr const char *s3_caps[]{"triangle-meshes", "so4-isometries", "stereographic-projection",
  "dynamic-drawing-meshes", "s3-octahedral-cells", "s3-octahedral-tangent-cuts"};
using E = GeometryEngine;
// Engine selection and geometric traits belong to the backend, never to the
// abstract executor. The legacy adapter has no realization-envelope support.
const GeometryBackendDescriptor descriptors[]{
  {"artifact-cube-euclidean",1,E::spatial,false,true,false,false,false,false,false,legacy_cube_caps},
  {"cube-euclidean",1,E::spatial,true,true,false,false,false,false,false,spatial_caps},
  {"cube-port-diagram",1,E::spatial,true,true,true,false,false,false,false,spatial_caps},
  {"cube-spherical",1,E::spatial,true,true,false,false,true,false,false,spatial_caps},
  {"polyhedral-euclidean",1,E::spatial,true,false,false,true,false,false,false,spatial_caps},
  {"polyhedral-port-diagram",1,E::spatial,true,false,true,true,false,false,false,spatial_caps},
  {"polyhedral-spherical",1,E::spatial,true,false,false,true,true,false,false,spatial_caps},
  {"articulated-euclidean",1,E::spatial,true,false,false,false,false,true,false,spatial_caps},
  {"articulated-port-diagram",1,E::spatial,true,false,true,false,false,true,false,spatial_caps},
  {"articulated-spherical",1,E::spatial,true,false,false,false,true,true,false,spatial_caps},
  {"flat-quotient",1,E::spatial,true,false,false,false,false,true,true,spatial_caps},
  {"s3-cell-turning",1,E::spherical4,true,false,false,false,false,false,false,s3_caps},
};
}
std::span<const GeometryBackendDescriptor> geometry_backend_descriptors() { return descriptors; }
const GeometryBackendDescriptor &geometry_backend(const std::string &id) {
  for (const auto &descriptor : descriptors) if (id == descriptor.id) return descriptor;
  throw DiagnosticError("realization.unsupported", "/backend", "Unknown geometric backend.");
}
void validate_backend_capabilities(const GeometryBackendDescriptor &backend, const Json &capabilities) {
  if (!capabilities.is_array() || capabilities.size() > 64)
    throw DiagnosticError("realization.capability", "/requiredCapabilities", "Expected at most 64 backend capabilities.");
  std::set<std::string> seen;
  for (const auto &cap : capabilities) {
    if (!cap.is_string() || !seen.insert(cap.get<std::string>()).second ||
        std::none_of(backend.capabilities.begin(), backend.capabilities.end(),
          [&](const char *supported) { return cap == supported; }))
      throw DiagnosticError("realization.capability", "/requiredCapabilities", "Unsupported or duplicate backend capability.");
  }
}
Json geometry_backend_kinds(bool include_legacy) {
  Json kinds = Json::array();
  for (const auto &descriptor : descriptors) if (include_legacy || descriptor.envelope) kinds.push_back(descriptor.id);
  return kinds;
}
} // namespace twisty::detail

namespace twisty {
Json geometry_backend_info() {
  Json backends = Json::array();
  std::set<std::string> capabilities{"uniform-cube-ports", "shared-geometry-definitions",
    "space-qualified-poses", "seekable-animations", "intrinsic-meshes"};
  for (const auto &backend : detail::geometry_backend_descriptors()) {
    Json requirements = Json::array();
    for (const auto *cap : backend.capabilities) { requirements.push_back(cap); capabilities.insert(cap); }
    backends.push_back({{"id",backend.id},{"version",backend.version},
      {"envelopeVersions",backend.envelope ? Json{contracts::realization_version} : Json::array()}, {"payloadVersions",backend.envelope ? Json{contracts::backend_payload_version} : Json::array()},
      {"capabilities",requirements}, {"space",backend.engine == detail::GeometryEngine::spherical4 ? "S3" : backend.flat ? "E2" : backend.spherical ? "S2" : "R3"}});
  }
  return {{"registryVersion",contracts::backend_registry_version},{"backends",backends},{"capabilities",capabilities}};
}
} // namespace twisty
