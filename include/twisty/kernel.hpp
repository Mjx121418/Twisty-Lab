#pragma once
#include "twisty/authoring/cube.hpp"
#include "twisty/authoring/gear.hpp"
#include "twisty/completion.hpp"
#include "twisty/geometry.hpp"
#include "twisty/geometry_backend.hpp"
#include "twisty/session.hpp"
#include "twisty/controller.hpp"

namespace twisty {
// API compatibility is independent of puzzle schema and semantic digests.
inline constexpr int kernel_api_version = 7;
inline constexpr const char *kernel_version = "0.7.0";
inline Json kernel_info() {
  return {{"apiVersion", kernel_api_version},
          {"version", kernel_version},
          {"documentKinds", {"relational-package", "relational-definition", "relational-gear-prototypes"}},
          {"definitionFormatVersion", 3},
          {"supportedDefinitionFormatVersions", {1,2,3}},
          {"sessionSchemaVersions", {2}},
          {"sessionFeatures", {"completed-choices", "shared-definitions"}},
          {"frameBufferVersion", 1},
          {"geometryAPIVersion", 1},
          {"controllerAPIVersion", 1},
          {"controllerCapabilities", {"state-backed-phases", "destination-intents", "scoped-previews", "piece-family-selection"}},
          {"poseBufferVersion", 1},
          {"intrinsicMeshBufferVersion", 1},
          {"languageAPIVersion", 3},
          {"abstractCapabilities", {"placement-actions", "reference-twist-families", "compact-transports", "rectangular-position-maps"}},
          {"authoringLibraries", {"cube3", "bandaged-uf-ufr", "gear-family"}},
          {"catalogKinds",
           {"relational-cube-bindings", "relational-port-bindings", "relational-articulated-bindings"}},
          {"ruleModules", {"relational@1"}},
          {"realizationKinds", [] { Json kinds = Json::array();
            const auto discovery = geometry_backend_info();
            for (const auto &backend : discovery.at("backends"))
              if (!backend.at("envelopeVersions").empty()) kinds.push_back(backend.at("id"));
            return kinds; }()},
          {"geometryBackends", geometry_backend_info()},
          {"geometryCapabilities", geometry_backend_info().at("capabilities")}};
}
} // namespace twisty
