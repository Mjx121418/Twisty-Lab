#include "twisty/authoring/gear.hpp"
#include "twisty/symmetry.hpp"
#include <algorithm>
#include <set>

namespace twisty::language {
namespace {
const std::vector<std::string> faces{"U", "R", "F", "D", "L", "B"};
std::string gear_key(const std::string &base, int phase) {
  return "gear-" + base + "/p" + std::to_string(phase);
}
struct Placement {
  std::string key, base, slot;
  std::vector<std::string> labels;
  int phase = 0;
};
Json chosen(const std::string &domain, const std::string &move) {
  return {{"chosen", {{"domain", domain}, {"moves", {move}}}}};
}
void expect(bool value, const char *message) {
  if (!value)
    throw core::Error("library.gear", message);
}
} // namespace

CubeModule gear_module(const std::vector<std::string> &geared_edges, const std::string &puzzle_id) {
  expect(!puzzle_id.empty() && puzzle_id.size() <= 256, "Expected a nonempty puzzle ID up to 256 bytes");
  // Reuse the new cube authoring library, never the legacy compiler/executor.
  auto base = cube_module(false);
  std::set<std::string> available, geared;
  for (const auto &piece : base.source.at("domains").at(2).at("pieces"))
    available.insert(piece.at("id").get<std::string>().substr(5));
  for (const auto &edge : geared_edges)
    expect(available.contains(edge) && geared.insert(edge).second, "Name distinct cube edge IDs");
  auto source = base.source, catalog = base.catalog;
  source["id"] = puzzle_id;
  source["relations"] = Json::array();
  source["requests"] = Json::array();
  catalog["kind"] = "relational-articulated-bindings";
  catalog["puzzleId"] = puzzle_id;
  catalog["requests"] = Json::object();
  catalog["events"] = Json::object();
  catalog["moveInverses"] = Json::object();
  Json cog_domain{{"id", "gear-edge"},
                  {"placements", Json::array()},
                  {"pieces", Json::array()},
                  {"moves", Json::array()}};
  Json cog_binding{{"id", "gear-edge"},
                   {"placements", Json::array()},
                   {"pieces", Json::array()},
                   {"localPorts", {"0", "1"}}};
  std::map<std::string, std::vector<Placement>> placements;
  for (auto &domain : source["domains"]) {
    const auto family = domain.at("id").get<std::string>();
    domain["moves"] = Json::array();
    for (const auto &key : domain.at("placements")) {
      const auto q = key.get<std::string>();
      const auto &entry = catalog.at("placements").at(family + "/" + q);
      Placement p{q, q, entry.at("resources").at(0).get<std::string>(), {}};
      for (const auto &[_, port] : entry.at("ports").items())
        p.labels.push_back(port.at("attachment").get<std::string>());
      placements[family].push_back(p);
      if (family != "edge")
        continue;
      for (int phase = 0; phase < 6; ++phase) {
        auto cog = p;
        cog.key = gear_key(q, phase);
        cog.phase = phase;
        placements["gear-edge"].push_back(cog);
        cog_domain["placements"].push_back(cog.key);
        const auto qualified = "gear-edge/" + cog.key;
        cog_binding["placements"].push_back(qualified);
        auto metadata = entry;
        for (auto &[_, port] : metadata["ports"].items())
          port["attachment"] = port.at("attachment").get<std::string>() + "/p" + std::to_string(phase);
        catalog["placements"][qualified] = std::move(metadata);
      }
    }
    if (family != "edge")
      continue;
    auto plain = Json::array(), plain_ids = Json::array();
    for (const auto &piece : domain.at("pieces")) {
      const auto id = piece.at("id").get<std::string>();
      if (!geared.contains(id.substr(5))) {
        plain.push_back(piece);
        plain_ids.push_back(id);
        continue;
      }
      const auto home = gear_key(piece.at("initial").get<std::string>(), 0);
      cog_domain["pieces"].push_back({{"id", id}, {"initial", home}});
      cog_binding["pieces"].push_back(id);
      catalog["pieces"][id]["domain"] = "gear-edge";
      catalog["pieces"][id]["home"] = "gear-edge/" + home;
    }
    domain["pieces"] = std::move(plain);
    catalog["domains"][2]["pieces"] = std::move(plain_ids);
  }
  source["domains"].push_back(std::move(cog_domain));
  catalog["domains"].push_back(std::move(cog_binding));
  const auto group = catalog.at("symmetry").get<std::vector<symmetry::Permutation>>();
  std::map<std::string, symmetry::Permutation> turns;
  for (const auto &g : group)
    turns.emplace(faces[g[0]],
                  symmetry::compose(g, symmetry::compose({0, 2, 4, 3, 5, 1}, symmetry::inverse(g))));
  for (const auto &[face, clockwise] : turns) {
    const auto opposite = faces[(std::find(faces.begin(), faces.end(), face) - faces.begin() + 3) % 6];
    auto middle = Json::array();
    for (const auto &q : placements.at("gear-edge"))
      if (q.slot.find(face) == std::string::npos && q.slot.find(opposite) == std::string::npos)
        middle.push_back("gear-edge/" + q.key);
    auto terms = Json::array();
    for (const auto &piece : source.at("domains").at(3).at("pieces"))
      terms.push_back({{"at", {{"piece", piece.at("id")}, {"placements", middle}}}});
    for (bool ordinary : {false, true})
      for (bool reversed : {false, true}) {
        const auto id = face + (ordinary ? "q" : "") + (reversed ? "'" : "");
        const auto inverse_id = face + (ordinary ? "q" : "") + (reversed ? "" : "'");
        const auto permutation = reversed ? symmetry::inverse(clockwise) : clockwise;
        const auto opposite_permutation = symmetry::inverse(permutation);
        auto tuple = Json::object(), relation = Json::array();
        auto unavailable = Json::array();
        for (auto &domain : source["domains"]) {
          const auto family = domain.at("id").get<std::string>();
          auto support = Json::array(), transport = Json::object();
          for (const auto &q : placements.at(family)) {
            const bool outer = q.slot.find(face) != std::string::npos;
            const bool other = q.slot.find(opposite) != std::string::npos;
            if (!outer && !other && ordinary && family == "gear-edge")
              unavailable.push_back(family + "/" + q.key);
            if ((ordinary && !outer) || (!ordinary && !outer && !other && family != "gear-edge"))
              continue;
            auto labels = q.labels;
            if (outer || other)
              for (auto &label : labels) {
                const auto index = std::find(faces.begin(), faces.end(), label) - faces.begin();
                label = faces[(outer ? permutation : opposite_permutation)[index]];
              }
            const auto base_family = family == "gear-edge" ? "edge" : family;
            auto target = base_family + ":";
            for (std::size_t i = 0; i < labels.size(); ++i)
              target += (i ? "-" : "") + labels[i];
            if (family == "gear-edge") {
              const auto phase = (q.phase + (!outer && !other ? (reversed ? -1 : 1) : 0) + 6) % 6;
              target = gear_key(target, phase);
            }
            support.push_back(q.key);
            transport[q.key] = target;
          }
          const auto event = family + "/" + id + ".motion";
          domain["moves"].push_back(
              {{"id", id}, {"support", support}, {"transport", transport}, {"event", event}});
          tuple[family] = id;
          relation.push_back(chosen(family, id));
          catalog["events"][event] = {{"domain", family}, {"request", id}};
          catalog["moveInverses"][family][id] = inverse_id;
        }
        // Conditional coupling is a relation on the complete local tuple. Counts
        // use the same IN/RANGE IR as occupancy and cross-domain obstruction.
        relation.push_back(
            {{"count", {{"min", ordinary ? 0 : 1}, {"max", ordinary ? 0 : 4}, {"of", terms}}}});
        source["requests"].push_back({{"id", id},
                                      {"relation", {{"all", relation}}},
                                      {"profiles", {{{"id", "turn"}, {"choices", tuple}}}},
                                      {"defaultProfile", "turn"}});
        catalog["requests"][id] = {{"inverse", inverse_id},
                                   {"family", face},
                                   {"event", id + ".coupled"},
                                   {"profile", "turn"},
                                   {"unavailablePlacements", unavailable}};
      }
  }
  auto invariant = Json::array();
  for (const auto &slot : catalog.at("resources")) {
    auto terms = Json::array();
    for (const auto &domain : source.at("domains")) {
      const auto family = domain.at("id").get<std::string>();
      auto at_slot = Json::array();
      for (const auto &q : placements.at(family))
        if (q.slot == slot.get<std::string>())
          at_slot.push_back(family + "/" + q.key);
      if (at_slot.empty())
        continue;
      for (const auto &piece : domain.at("pieces"))
        terms.push_back({{"at", {{"piece", piece.at("id")}, {"placements", at_slot}}}});
    }
    invariant.push_back({{"count", {{"min", 0}, {"max", 1}, {"of", terms}}}});
  }
  source["invariant"] = {{"all", invariant}};
  return {std::move(source), std::move(catalog)};
}
CubeModule gear_module_source(const Json &source) {
  expect(source.is_object() && source.size() == 4 &&
             source.value("kind", std::string{}) == "relational-gear-prototypes" &&
             source.at("formatVersion").is_number_integer() && source.at("formatVersion") == 1 &&
             source.at("gearedEdges").is_array() && source.at("puzzleId").is_string(),
         "Expected relational-gear-prototypes format 1");
  return gear_module(source.at("gearedEdges").get<std::vector<std::string>>(),
                     source.at("puzzleId").get<std::string>());
}
std::string gear_module_json(const std::string &text) {
  auto module = gear_module_source(Json::parse(text));
  return Json{{"source", module.source}, {"catalog", module.catalog}}.dump();
}
} // namespace twisty::language
