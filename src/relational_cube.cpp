#include "twisty/relational_cube.hpp"
#include "twisty/symmetry.hpp"
#include <algorithm>
#include <map>

namespace twisty::relational {
namespace {
using symmetry::Permutation;
using Labels = std::vector<std::string>;
const Labels faces{"U", "R", "F", "D", "L", "B"};
const std::map<std::string, Labels> corners{
    {"UFR", {"U", "R", "F"}}, {"UFL", {"U", "F", "L"}}, {"UBL", {"U", "L", "B"}}, {"UBR", {"U", "B", "R"}},
    {"DFR", {"D", "F", "R"}}, {"DFL", {"D", "L", "F"}}, {"DBL", {"D", "B", "L"}}, {"DBR", {"D", "R", "B"}}};
const std::map<std::string, Labels> edges{{"UF", {"U", "F"}}, {"UR", {"U", "R"}}, {"UB", {"U", "B"}},
                                          {"UL", {"U", "L"}}, {"FR", {"F", "R"}}, {"BR", {"B", "R"}},
                                          {"BL", {"B", "L"}}, {"FL", {"F", "L"}}, {"DF", {"D", "F"}},
                                          {"DR", {"D", "R"}}, {"DB", {"D", "B"}}, {"DL", {"D", "L"}}};

std::string cell(Labels labels) {
  std::sort(labels.begin(), labels.end());
  for (const auto *slots : {&corners, &edges})
    for (const auto &[id, values] : *slots) {
      auto sorted = values;
      std::sort(sorted.begin(), sorted.end());
      if (labels == sorted)
        return id;
    }
  if (labels.size() == 1)
    return labels.front();
  throw flat::Error("library.cube", "Unknown cube slot");
}
Labels acted(const Labels &labels, const Permutation &permutation) {
  Labels result;
  for (const auto &label : labels) {
    const auto i = std::find(faces.begin(), faces.end(), label) - faces.begin();
    result.push_back(faces.at(permutation.at(i)));
  }
  return result;
}
std::string key(const std::string &family, const Labels &labels) {
  auto result = family + ":";
  for (std::size_t i = 0; i < labels.size(); ++i)
    result += (i ? "-" : "") + labels[i];
  return result;
}
Labels resources(const std::string &family, const Labels &labels) {
  if (family == "bandage")
    return {cell({labels[0], labels[1]}), cell({labels[2], labels[3], labels[4]})};
  return {cell(labels)};
}
Json ports(const std::string &family, const Labels &labels) {
  const auto occupied = resources(family, labels);
  auto result = Json::object();
  for (std::size_t i = 0; i < labels.size(); ++i)
    result[std::to_string(i)] = {{"cell", occupied[family == "bandage" && i >= 2 ? 1 : 0]},
                                 {"attachment", labels[i]}};
  return result;
}
Json chosen(const std::string &domain, const std::string &move) {
  return {{"chosen", {{"domain", domain}, {"moves", Json::array({move})}}}};
}
} // namespace

CubeModule cube_module(bool bandaged) {
  const auto group = symmetry::enumerate_group({{0, 2, 4, 3, 5, 1}, {5, 1, 0, 2, 4, 3}}, 24);
  std::map<std::string, Labels> prototypes{
      {"center", {"U"}}, {"corner", {"U", "R", "F"}}, {"edge", {"U", "F"}}};
  if (bandaged)
    prototypes["bandage"] = {"U", "F", "U", "R", "F"};
  const auto id = bandaged ? "bandaged-uf-ufr" : "cube3";
  Json source{{"kind", "relational-puzzle"}, {"languageVersion", 1},       {"id", id},
              {"domains", Json::array()},    {"relations", Json::array()}, {"requests", Json::array()}};
  Json catalog{{"kind", "relational-cube-bindings"},
               {"version", 1},
               {"puzzleId", id},
               {"domains", Json::array()},
               {"pieces", Json::object()},
               {"placements", Json::object()},
               {"requests", Json::object()},
               {"faceLabels", faces},
               {"symmetry", group}};
  // This is authoring data. Slot incidence is compiled away into ordinary IN/count
  // predicates. The flat executor never sees cells, footprints, or these families.
  std::map<std::string, std::map<std::string, Labels>> placements;
  for (const auto &[family, prototype] : prototypes) {
    for (const auto &g : group) {
      const auto labels = acted(prototype, g);
      placements[family].emplace(key(family, labels), labels);
    }
    Json domain{
        {"id", family}, {"placements", Json::array()}, {"pieces", Json::array()}, {"moves", Json::array()}};
    auto local_ports = Json::array();
    for (std::size_t i = 0; i < prototype.size(); ++i)
      local_ports.push_back(std::to_string(i));
    Json domain_binding{{"id", family},
                        {"pieces", Json::array()},
                        {"placements", Json::array()},
                        {"localPorts", local_ports}};
    for (const auto &[q, labels] : placements[family]) {
      domain["placements"].push_back(q);
      domain_binding["placements"].push_back(family + "/" + q);
      catalog["placements"][family + "/" + q] = {{"resources", resources(family, labels)},
                                                 {"ports", ports(family, labels)}};
    }
    const auto piece = [&](const std::string &piece_id, const Labels &labels) {
      domain["pieces"].push_back({{"id", piece_id}, {"initial", key(family, labels)}});
      domain_binding["pieces"].push_back(piece_id);
      auto port_labels = Json::object();
      for (std::size_t i = 0; i < labels.size(); ++i)
        port_labels[std::to_string(i)] = labels[i];
      catalog["pieces"][piece_id] = {
          {"domain", family}, {"home", family + "/" + key(family, labels)}, {"portLabels", port_labels}};
    };
    if (family == "center") {
      auto sorted = faces;
      std::sort(sorted.begin(), sorted.end());
      for (const auto &face : sorted)
        piece("center/" + face, {face});
    } else if (family == "bandage") {
      piece("bandage/UF-UFR", prototype);
    } else {
      for (const auto &[slot, labels] : family == "corner" ? corners : edges) {
        if (bandaged && ((family == "corner" && slot == "UFR") || (family == "edge" && slot == "UF")))
          continue;
        piece(family + "/" + slot, labels);
      }
    }
    source["domains"].push_back(std::move(domain));
    catalog["domains"].push_back(std::move(domain_binding));
  }
  std::map<std::string, Permutation> turns;
  for (const auto &g : group)
    turns.emplace(faces[g[0]],
                  symmetry::compose(g, symmetry::compose({0, 2, 4, 3, 5, 1}, symmetry::inverse(g))));
  for (const auto &[face, clockwise] : turns) {
    for (bool reversed : {false, true}) {
      const auto movement = face + (reversed ? "'" : "");
      const auto event = face + (reversed ? ".counterclockwise" : ".clockwise");
      const auto permutation = reversed ? symmetry::inverse(clockwise) : clockwise;
      auto tuple = Json::object(), restrictions = Json::array(), blockers = Json::array(),
           drivers = Json::array();
      for (auto &domain : source["domains"]) {
        const auto family = domain.at("id").get<std::string>();
        auto support = Json::array(), transport = Json::object();
        for (const auto &[q, labels] : placements.at(family)) {
          const auto occupied = resources(family, labels);
          const auto selected = std::count_if(occupied.begin(), occupied.end(), [&](const auto &slot) {
            return slot.find(face) != std::string::npos;
          });
          if (selected == static_cast<std::ptrdiff_t>(occupied.size())) {
            support.push_back(q);
            transport[q] = key(family, acted(labels, permutation));
          } else if (selected != 0) {
            blockers.push_back(family + "/" + q);
          }
        }
        domain["moves"].push_back(
            {{"id", movement}, {"support", support}, {"transport", transport}, {"event", event}});
        tuple[family] = movement;
        restrictions.push_back(chosen(family, movement));
        drivers.push_back(chosen(family, movement));
      }
      if (bandaged && !blockers.empty())
        source["relations"].push_back(
            {{"id", "rigid-compound/" + movement},
             {"predicate",
              {{"implies",
                Json::array(
                    {{{"any", drivers}},
                     {{"not", {{"at", {{"piece", "bandage/UF-UFR"}, {"placements", blockers}}}}}}})}}}});
      source["requests"].push_back({{"id", movement},
                                    {"relation", {{"all", restrictions}}},
                                    {"profiles", Json::array({{{"id", "turn"}, {"choices", tuple}}})},
                                    {"defaultProfile", "turn"}});
      catalog["requests"][movement] = {
          {"inverse", face + (reversed ? "" : "'")}, {"family", face}, {"event", event}, {"profile", "turn"}};
    }
  }
  // Every slot must have exactly one occupant, including both slots occupied by
  // the single rigid compound identity. These are ordinary cardinality formulas.
  Labels cells = faces;
  for (const auto *slots : {&corners, &edges})
    for (const auto &[slot, labels] : *slots) {
      (void)labels;
      cells.push_back(slot);
    }
  std::sort(cells.begin(), cells.end());
  auto invariants = Json::array();
  for (const auto &slot : cells) {
    auto terms = Json::array();
    for (const auto &domain : source["domains"]) {
      const auto family = domain.at("id").get<std::string>();
      auto at_slot = Json::array();
      for (const auto &[q, labels] : placements.at(family)) {
        const auto occupied = resources(family, labels);
        if (std::find(occupied.begin(), occupied.end(), slot) != occupied.end())
          at_slot.push_back(family + "/" + q);
      }
      if (!at_slot.empty())
        for (const auto &p : domain.at("pieces"))
          terms.push_back({{"at", {{"piece", p.at("id")}, {"placements", at_slot}}}});
    }
    invariants.push_back({{"count", {{"min", 1}, {"max", 1}, {"of", terms}}}});
  }
  source["invariant"] = {{"all", invariants}};
  catalog["resources"] = cells;
  return {std::move(source), std::move(catalog)};
}
std::string cube_module_json(bool bandaged) {
  const auto module = cube_module(bandaged);
  return Json{{"source", module.source}, {"catalog", module.catalog}}.dump();
}
} // namespace twisty::relational
