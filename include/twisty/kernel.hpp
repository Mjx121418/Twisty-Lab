#pragma once
#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include "twisty/completion.hpp"
#include "twisty/authoring/cube.hpp"
#include "twisty/authoring/gear.hpp"

namespace twisty {
// API compatibility is independent of puzzle schema and semantic digests.
inline constexpr int kernel_api_version = 4;
inline constexpr const char *kernel_version = "0.4.0";
inline Json kernel_info() {
  return {
      {"apiVersion", kernel_api_version},
      {"version", kernel_version},
      {"documentKinds", {"relational-package", "relational-definition", "relational-gear-prototypes"}},
      {"definitionFormatVersion", 1},
      {"sessionSchemaVersions", {2}},
      {"sessionFeatures", {"completed-choices", "shared-definitions"}},
      {"frameBufferVersion", 1},
      {"geometryAPIVersion", 1},
      {"poseBufferVersion", 1},
      {"languageAPIVersion", 1},
      {"authoringLibraries", {"cube3", "bandaged-uf-ufr", "gear-family"}},
      {"catalogKinds", {"relational-cube-bindings", "relational-port-bindings", "relational-articulated-bindings"}},
      {"ruleModules", {"relational@1"}},
      {"realizationKinds",
       {"cube-euclidean", "cube-port-diagram", "cube-spherical", "polyhedral-euclidean",
        "polyhedral-port-diagram", "polyhedral-spherical", "articulated-euclidean",
        "articulated-port-diagram", "articulated-spherical", "flat-quotient"}},
      {"geometryCapabilities", {"triangle-meshes", "rigid-transforms", "quotient-surface", "quotient-orbifold",
                                "shared-geometry-definitions", "space-qualified-poses", "seekable-animations"}}};
}
} // namespace twisty
