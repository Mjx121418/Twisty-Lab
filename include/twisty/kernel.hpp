#pragma once
#include "twisty/compiler.hpp"
#include "twisty/geometry.hpp"
#include "twisty/session.hpp"

namespace twisty {
// API compatibility is independent of puzzle schema and semantic digests.
inline constexpr int kernel_api_version = 1;
inline constexpr const char *kernel_version = "0.1.0";
inline Json kernel_info() {
  return {{"apiVersion", kernel_api_version},
          {"version", kernel_version},
          {"documentSchemaVersion", 1},
          {"frameBufferVersion", 1},
          {"ruleModules", {"finite-footprint@1", "finite-placement-relations@1"}},
          {"realizationKinds",
           {"cube-euclidean", "cube-port-diagram", "polyhedral-euclidean", "polyhedral-port-diagram"}},
          {"geometryCapabilities", {"triangle-meshes", "rigid-transforms"}}};
}
} // namespace twisty
