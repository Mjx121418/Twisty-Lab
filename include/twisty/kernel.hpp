#pragma once
#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include "twisty/relational_completion.hpp"
#include "twisty/relational_cube.hpp"
#include "twisty/relational_gear.hpp"

namespace twisty {
// API compatibility is independent of puzzle schema and semantic digests.
inline constexpr int kernel_api_version = 3;
inline constexpr const char *kernel_version = "0.3.0";
inline Json kernel_info() {
  return {
      {"apiVersion", kernel_api_version},
      {"version", kernel_version},
      {"documentKinds", {"relational-package", "relational-definition", "relational-gear-prototypes"}},
      {"relationalDefinitionVersion", 1},
      {"sessionSchemaVersions", {2}},
      {"sessionFeatures", {"completed-choices", "shared-definitions"}},
      {"frameBufferVersion", 1},
      {"experimentalAPIs", {{"relational", 1}}},
      {"relationalLibraries", {"cube3", "bandaged-uf-ufr", "gear-family"}},
      {"relationalCatalogKinds", {"relational-cube-bindings", "relational-port-bindings", "relational-articulated-bindings"}},
      {"ruleModules", {"relational@1"}},
      {"realizationKinds",
       {"cube-euclidean", "cube-port-diagram", "cube-spherical", "polyhedral-euclidean",
        "polyhedral-port-diagram", "polyhedral-spherical", "articulated-euclidean",
        "articulated-port-diagram", "articulated-spherical"}},
      {"geometryCapabilities", {"triangle-meshes", "rigid-transforms"}}};
}
} // namespace twisty
