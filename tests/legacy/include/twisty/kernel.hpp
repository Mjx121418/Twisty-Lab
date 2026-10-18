#pragma once
#include "twisty/compiler.hpp"
#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include "twisty/completion.hpp"
#include "twisty/authoring/cube.hpp"
#include "twisty/authoring/gear.hpp"

namespace twisty {
// API compatibility is independent of puzzle schema and semantic digests.
inline constexpr int kernel_api_version = 2;
inline constexpr const char *kernel_version = "0.2.0";
inline Json kernel_info() {
  return {
      {"apiVersion", kernel_api_version},
      {"version", kernel_version},
      {"documentSchemaVersion", 1},
      {"definitionFormatVersion", 1},
      {"sessionSchemaVersions", {1, 2}},
      {"sessionFeatures", {"completed-choices", "shared-definitions"}},
      {"frameBufferVersion", 1},
      {"experimentalAPIs", {{"relational", 1}}},
      {"authoringLibraries", {"cube3", "bandaged-uf-ufr", "gear-family"}},
      {"catalogKinds", {"relational-cube-bindings", "relational-port-bindings", "relational-articulated-bindings"}},
      {"ruleModules", {"relational@1", "finite-footprint@1", "finite-placement-relations@1", "finite-placement-relations@2"}},
      {"realizationKinds",
       {"cube-euclidean", "cube-port-diagram", "cube-spherical", "polyhedral-euclidean",
        "polyhedral-port-diagram", "polyhedral-spherical", "articulated-euclidean",
        "articulated-port-diagram", "articulated-spherical"}},
      {"geometryCapabilities", {"triangle-meshes", "rigid-transforms"}}};
}
} // namespace twisty
