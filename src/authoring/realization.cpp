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
  const auto execution = Execution::from_source(source);
  const auto &def = *execution->definition(); const auto puzzle = execution->compiled_puzzle();
  const auto &b = puzzle->bindings;
  auto abstract = execution->artifact_json(); auto manifest = interpretation_artifact(*execution, abstract);
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
  for (const auto &piece_id : b.pieces) {
    const auto p = std::find_if(def.pieces.begin(), def.pieces.end(), [&](const auto &piece) { return piece.id == piece_id; });
    kinds.push_back(backend.starts_with("cube-") && p->type == "bandage" ? "cube-bandage" : "rigid");
  }
  if (backend.starts_with("cube-")) {
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
  Json payload{{"payloadVersion", 1}, {"requestBindings", bindings}, {"pieceKinds", kinds}, {"geometry", geometry}};
  const auto payload_text = payload.dump();
  Json tracks = Json::array();
  for (Index c = 0; c < manifest.at("certificates").size(); ++c) {
    const auto request = manifest.at("certificates")[c].at("request").get<Index>();
    tracks.push_back({{"certificate", c}, {"pathId", bindings[request].at("pathId")}});
  }
  Json envelope{{"kind", "twisty-realization"}, {"contractVersion", 1}, {"id", geometry.at("id")},
    {"interpretationDigest", manifest.at("interpretationDigest")}, {"requiredCapabilities", capabilities},
    {"backend", {{"id", backend}, {"version", 1}}}, {"payload", {{"path", "geometry.json"}, {"sha256", sha256(payload_text)}}},
    {"eventTracks", tracks}};
  artifact::seal(envelope, "realizationDigest");
  GeometryDefinition verified(Execution::from_artifact(abstract.dump()), manifest.dump(), envelope.dump(), payload_text);
  return {{"abstract", abstract}, {"interpretation", manifest}, {"realization", envelope}, {"payload", payload_text}};
}
} // namespace twisty::language
