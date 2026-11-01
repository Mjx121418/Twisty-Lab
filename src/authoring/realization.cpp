#include "twisty/contracts.hpp"
#include "realization_internal.hpp"
#include "../interpreter/backend_contract.hpp"
#include "twisty/authoring/realization.hpp"
#include "twisty/authoring/artifact.hpp"
#include "twisty/artifact.hpp"
#include "twisty/execution.hpp"
#include "twisty/geometry.hpp"
#include "../interpreter/artifact_realization.hpp"
#include <numbers>

namespace twisty::language {
namespace {
Index find(const std::vector<std::string> &values, const std::string &value) {
  const auto it = std::find(values.begin(), values.end(), value);
  if (it == values.end()) throw DiagnosticError("artifact.reference", "/authoring", "Unknown authoring identity.");
  return static_cast<Index>(it - values.begin());
}
Json rename_keys(Json value, const std::map<std::string, std::string> &names) {
  Json result = Json::object();
  for (auto &[key, entry] : value.items()) result[names.at(key)] = std::move(entry);
  return result;
}
}
Json realization_artifacts(const std::string &source, const std::string &realization) {
  return realization_artifacts(Execution::from_source(source), realization);
}
Json realization_artifacts(std::shared_ptr<const Execution> execution, const std::string &realization) {
  RealizationExport prepared(std::move(execution));
  auto result = prepared.view(realization);
  result["abstract"] = prepared.abstract();
  result["interpretation"] = prepared.interpretation();
  return result;
}
RealizationExport::RealizationExport(std::shared_ptr<const Execution> execution) : execution_(std::move(execution)) {
  if (!execution_ || execution_->source_free()) throw DiagnosticError("authoring.execution", "/", "A source-compiled authoring context is required.");
  abstract_ = execution_->artifact_json();
  interpretation_ = interpretation_artifact(*execution_, abstract_);
  runtime_ = Execution::from_artifact(abstract_.dump());
}
Json RealizationExport::view(const std::string &realization) const {
  const auto &def = *execution_->definition(); const auto puzzle = execution_->compiled_puzzle();
  const auto &b = puzzle->bindings;
  const auto &abstract = abstract_, &manifest = interpretation_;
  auto geometry = Json::parse(realization);
  if (geometry.at("compatibleDefinitionDigest") != def.digest)
    throw DiagnosticError("realization.digest_mismatch", "/compatibleDefinitionDigest", "Authoring realization pins another source definition.");
  geometry["compatibleDefinitionDigest"] = abstract.at("executionDigest");
  if (geometry.contains("controlLayout") && geometry.at("controlLayout").at("version") != 2) geometry.erase("controlLayout");
  Json bindings = Json::array(), kinds = Json::array();
  std::map<std::string, std::string> paths, groups;
  for (Index r = 0; r < b.requests.size(); ++r) {
    const auto &op = def.operations[r]; const auto path = "path-" + std::to_string(r);
    paths.emplace(op.id, path);
    if (!groups.contains(op.family)) groups.emplace(op.family, "group-" + std::to_string(groups.size()));
    std::vector<Index> resources; for (const auto &resource : op.cells) resources.push_back(find(def.cells, resource));
    bindings.push_back({{"pathId", path}, {"group", groups.at(op.family)}, {"resources", resources}});
  }
  const auto backend = geometry.at("kind").get<std::string>();
  const auto &descriptor = twisty::detail::geometry_backend(backend);
  for (const auto &piece_id : b.pieces) {
    const auto p = std::find_if(def.pieces.begin(), def.pieces.end(), [&](const auto &piece) { return piece.id == piece_id; });
    kinds.push_back(descriptor.cube && p->type == "bandage" ? "cube-bandage" : "rigid");
  }
  if (descriptor.cube) {
    const std::map<std::string, std::vector<int>> axes{{"U", {0,1,0}}, {"D", {0,-1,0}}, {"R", {1,0,0}}, {"L", {-1,0,0}}, {"F", {0,0,1}}, {"B", {0,0,-1}}};
    Json tracks = Json::object();
    for (const auto &op : def.operations)
      tracks[op.id] = {{"axis", axes.at(op.family)}, {"angle", (op.transport.ends_with(".counterclockwise") ? 1 : -1) * std::numbers::pi / 2}};
    geometry["explicitCubeTracks"] = std::move(tracks);
    geometry["requiredCapabilities"].push_back("explicit-cube-tracks");
  }
  auto capabilities = geometry.at("requiredCapabilities").get<std::vector<std::string>>();
  std::sort(capabilities.begin(), capabilities.end()); geometry["requiredCapabilities"] = capabilities;
  std::map<std::string, std::string> all_placements;
  Json new_tables = Json::object();
  for (Index d = 0; d < def.domains.size(); ++d) {
    const auto &domain = def.domains[d]; std::map<std::string, std::string> names;
    for (const auto &q : domain.placements) {
      const auto global = b.placements.at(find(b.placements, domain.id + "/" + q.key));
      names.emplace(q.key, global); all_placements.emplace(q.key, global);
    }
    if (geometry.contains("domains")) {
      auto &frames = geometry["domains"][domain.id]["placementFrames"];
      frames = rename_keys(std::move(frames), names);
    }
    if (geometry.contains("componentsByDomain")) for (auto &component : geometry["componentsByDomain"][domain.id]) {
      component["placementFrames"] = rename_keys(std::move(component["placementFrames"]), names);
      auto remap_entries = [&](Json &table) {
        for (auto &entries : table) if (!entries.is_object() || entries.value("kind", Json{}) != "transport-regions")
          entries = rename_keys(std::move(entries), names);
      };
      if (component.at("operationTracks").is_string()) {
        const auto old = component.at("operationTracks").get<std::string>();
        const auto table_id = "domain-" + std::to_string(d) + "/" + old;
        if (!new_tables.contains(table_id)) {
          auto table = geometry.at("operationTrackTables").at(old); remap_entries(table);
          new_tables[table_id] = std::move(table);
        }
        component["operationTracks"] = table_id;
      } else remap_entries(component["operationTracks"]);
    }
  }
  if (geometry.contains("placementFrames")) geometry["placementFrames"] = rename_keys(std::move(geometry["placementFrames"]), all_placements);
  if (geometry.contains("operationTrackTables")) geometry["operationTrackTables"] = std::move(new_tables);
  detail::remap_realization_requests(geometry, paths);
  Json payload{{"payloadVersion", contracts::backend_payload_version}, {"requestBindings", bindings}, {"pieceKinds", kinds}, {"geometry", geometry}};
  const auto payload_text = payload.dump();
  Json tracks = Json::array();
  for (Index c = 0; c < manifest.at("certificates").size(); ++c) {
    const auto request = manifest.at("certificates")[c].at("request").get<Index>();
    tracks.push_back({{"certificate", c}, {"pathId", bindings[request].at("pathId")}});
  }
  Json envelope{{"kind", contracts::realization_kind}, {"contractVersion", contracts::realization_version}, {"id", geometry.at("id")},
    {"interpretationDigest", manifest.at("interpretationDigest")}, {"requiredCapabilities", capabilities},
    {"backend", {{"id", backend}, {"version", descriptor.version}}}, {"payload", {{"path", "geometry.json"}, {"sha256", sha256(payload_text)}}},
    {"eventTracks", tracks}};
  artifact::seal(envelope, "realizationDigest");
  GeometryDefinition verified(runtime_, manifest.dump(), envelope.dump(), payload_text);
  return {{"realization", envelope}, {"payload", payload_text}};
}
} // namespace twisty::language
