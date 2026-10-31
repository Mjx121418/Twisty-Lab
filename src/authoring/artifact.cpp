#include "twisty/authoring/artifact.hpp"
#include "twisty/authoring/cube.hpp"
#include "twisty/artifact.hpp"
#include "twisty/execution.hpp"
#include <numbers>

namespace twisty::language {
namespace {
template <class T> Index find(const std::vector<T> &items, const T &item) {
  const auto it = std::find(items.begin(), items.end(), item);
  if (it == items.end()) throw DiagnosticError("artifact.reference", "/authoring", "Unknown authoring symbol.");
  return static_cast<Index>(it - items.begin());
}
} // namespace
Json cube_artifacts(bool bandaged) {
  auto module = cube_module(bandaged);
  const auto execution = Execution::from_source(Json{{"kind", "relational-package"},
    {"formatVersion", module.source.at("languageVersion")}, {"source", module.source}, {"catalog", module.catalog}}.dump());
  const auto puzzle = execution->compiled_puzzle(); const auto &b = puzzle->bindings;
  const auto &def = *execution->definition();
  std::vector<Index> ri; std::vector<std::vector<Index>> ci;
  for (const auto &op : def.operations) ri.push_back(def.operation_ids.at(op.inverse));
  for (const auto &values : b.choice_values) {
    std::vector<Index> inverses{0};
    for (Index value = 1; value < values.size(); ++value)
      inverses.push_back(find(values, def.operations[def.operation_ids.at(values[value])].inverse));
    ci.push_back(std::move(inverses));
  }
  auto abstract = artifact::encode(*puzzle, ri, ci);
  Json manifest{{"kind", "twisty-interpretation"}, {"contractVersion", 1}, {"executionDigest", abstract.at("executionDigest")},
    {"resources", def.cells}, {"domains", Json::array()}, {"pieces", Json::array()},
    {"placements", Json::array()}, {"certificates", Json::array()}};
  std::vector<Json> placements(b.placements.size());
  for (Index d = 0; d < def.domains.size(); ++d) {
    const auto &domain = def.domains[d]; Json ports = Json::array();
    for (const auto &[port, _] : domain.placements.front().ports) ports.push_back(port);
    const auto slot = find(b.choice_slots, domain.id);
    manifest["domains"].push_back({{"id", domain.id}, {"choiceSlot", slot}, {"ports", ports}});
    for (const auto &q : domain.placements) {
      const auto global = find(b.placements, domain.id + "/" + q.key);
      std::vector<Index> resources; Json bindings = Json::array();
      for (const auto &cell : q.footprint) resources.push_back(find(def.cells, cell));
      for (const auto &[id, port] : q.ports) bindings.push_back({{"id", id}, {"resource", find(def.cells, port.first)}, {"attachment", port.second}});
      placements[global] = {{"placement", global}, {"domain", d}, {"resources", resources}, {"ports", bindings}};
    }
  }
  for (auto &q : placements) manifest["placements"].push_back(std::move(q));
  for (Index p = 0; p < b.pieces.size(); ++p) {
    const auto found = std::find_if(def.pieces.begin(), def.pieces.end(), [&](const auto &x) { return x.id == b.pieces[p]; });
    Json labels = Json::array(); for (const auto &[port, material] : found->labels) labels.push_back({{"port", port}, {"material", material}});
    manifest["pieces"].push_back({{"piece", p}, {"domain", found->domain}, {"portLabels", labels}});
  }
  Json tracks = Json::object();
  const std::map<std::string, std::vector<int>> axes{{"U", {0,1,0}}, {"D", {0,-1,0}}, {"R", {1,0,0}}, {"L", {-1,0,0}}, {"F", {0,0,1}}, {"B", {0,0,-1}}};
  for (Index r = 0; r < b.requests.size(); ++r) {
    const auto &op = def.operations[r];
    tracks[op.id] = {{"axis", axes.at(op.family)}, {"angle", (op.transport.ends_with(".counterclockwise") ? 1 : -1) * std::numbers::pi / 2}};
    const auto &profile = b.requests[r].profiles[*b.requests[r].default_profile];
    for (Index d = 0; d < def.domains.size(); ++d) {
      const auto slot = find(b.choice_slots, def.domains[d].id), choice = profile.choices[slot];
      const auto range = b.movements[slot][choice]; if (!range.action_count) continue;
      std::vector<Index> actions, unavailable;
      for (Index i = 0; i < range.action_count; ++i) actions.push_back(range.first_action + i);
      for (Index q = 0; q < def.domains[d].placements.size(); ++q)
        if (op.roles[d][q] == 2) unavailable.push_back(find(b.placements, def.domains[d].id + "/" + def.domains[d].placements[q].key));
      manifest["certificates"].push_back({{"id", "track-" + std::to_string(r) + "-" + std::to_string(d)},
        {"domain", d}, {"request", r}, {"event", puzzle->program.data().actions[range.first_action].event},
        {"transport", b.move_maps[slot][choice]}, {"actions", actions}, {"unavailablePlacements", unavailable}});
    }
  }
  artifact::seal(manifest, "interpretationDigest");
  // Force the source-independent manifest validator before exporting.
  Execution::from_artifact(abstract.dump())->with_interpretation(manifest.dump());
  Json realization{{"schemaVersion", 1}, {"id", b.puzzle_id + "-artifact-euclidean-v1"}, {"kind", "artifact-cube-euclidean"},
    {"compatibleDefinitionDigest", abstract.at("executionDigest")}, {"compatibleInterpretationDigest", manifest.at("interpretationDigest")},
    {"requiredCapabilities", {"explicit-cube-tracks", "rigid-transforms", "triangle-meshes"}}, {"explicitCubeTracks", tracks}};
  return {{"abstract", abstract}, {"interpretation", manifest}, {"realization", realization}};
}
std::string cube_artifacts_json(bool bandaged) { return cube_artifacts(bandaged).dump(); }
} // namespace twisty::language
