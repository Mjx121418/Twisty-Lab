#pragma once
#include "twisty/authoring/cube.hpp"
#include "twisty/authoring/gear.hpp"
#include "twisty/completion.hpp"
#include "twisty/geometry.hpp"
#include "twisty/session.hpp"

namespace twisty {
// API compatibility is independent of puzzle schema and semantic digests.
inline constexpr int kernel_api_version = 5;
inline constexpr const char *kernel_version = "0.5.0";
inline Json kernel_info() {
  return {{"apiVersion", kernel_api_version},
          {"version", kernel_version},
          {"documentKinds", {"relational-package", "relational-definition", "relational-gear-prototypes"}},
          {"definitionFormatVersion", 2},
          {"supportedDefinitionFormatVersions", {1,2}},
          {"sessionSchemaVersions", {2}},
          {"sessionFeatures", {"completed-choices", "shared-definitions"}},
          {"frameBufferVersion", 1},
          {"geometryAPIVersion", 1},
          {"poseBufferVersion", 1},
          {"languageAPIVersion", 2},
          {"abstractCapabilities", {"placement-actions", "reference-twist-families", "compact-transports"}},
          {"authoringLibraries", {"cube3", "bandaged-uf-ufr", "gear-family"}},
          {"catalogKinds",
           {"relational-cube-bindings", "relational-port-bindings", "relational-articulated-bindings"}},
          {"ruleModules", {"relational@1"}},
          {"realizationKinds",
           {"cube-euclidean", "cube-port-diagram", "cube-spherical", "polyhedral-euclidean",
            "polyhedral-port-diagram", "polyhedral-spherical", "articulated-euclidean",
            "articulated-port-diagram", "articulated-spherical", "flat-quotient"}},
          {"geometryCapabilities",
           {"triangle-meshes", "rigid-transforms", "quotient-surface", "quotient-orbifold",
            "shared-geometry-definitions", "shared-rotation-tracks", "space-qualified-poses",
            "seekable-animations"}}};
}
} // namespace twisty
