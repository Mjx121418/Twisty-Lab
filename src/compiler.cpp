#include "twisty/compiler.hpp"
#include <algorithm>
#include <numeric>
#include <set>

namespace twisty {
namespace {
void validate_permutation(const Permutation &p) {
  auto sorted = p;
  std::sort(sorted.begin(), sorted.end());
  for (Index i = 0; i < sorted.size(); ++i)
    if (sorted[i] != i)
      throw DiagnosticError("symmetry.permutation", "/group/generators", "Generator is not a permutation.");
}
const std::vector<std::string> faces{"U", "R", "F", "D", "L", "B"};
const std::vector<std::vector<std::string>> corners{{"U", "R", "F"}, {"U", "F", "L"}, {"U", "L", "B"},
                                                    {"U", "B", "R"}, {"D", "F", "R"}, {"D", "L", "F"},
                                                    {"D", "B", "L"}, {"D", "R", "B"}};
const std::vector<std::vector<std::string>> edges{{"U", "F"}, {"U", "R"}, {"U", "B"}, {"U", "L"},
                                                  {"F", "R"}, {"B", "R"}, {"B", "L"}, {"F", "L"},
                                                  {"D", "F"}, {"D", "R"}, {"D", "B"}, {"D", "L"}};
std::string joined(const std::vector<std::string> &values, const std::string &delimiter = "") {
  std::string output;
  for (const auto &value : values) {
    if (!output.empty())
      output += delimiter;
    output += value;
  }
  return output;
}
std::string cell_for(std::vector<std::string> values) {
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const std::vector<std::string> corner_names{"UFR", "UFL", "UBL", "UBR", "DFR", "DFL", "DBL", "DBR"};
  const std::vector<std::string> edge_names{"UF", "UR", "UB", "UL", "FR", "BR",
                                            "BL", "FL", "DF", "DR", "DB", "DL"};
  for (std::size_t family = 0; family < 2; ++family) {
    const auto &candidates = family == 0 ? corners : edges;
    const auto &names = family == 0 ? corner_names : edge_names;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      auto test = candidates[i];
      std::sort(test.begin(), test.end());
      if (test == sorted)
        return names[i];
    }
  }
  if (values.size() == 1 && std::find(faces.begin(), faces.end(), values.front()) != faces.end())
    return values.front();
  throw DiagnosticError("symmetry.cell", "/group",
                        "Group action maps a prototype outside its abstract cell family.");
}
std::string acted(const std::string &face, const Permutation &p) {
  const auto found = std::find(faces.begin(), faces.end(), face);
  if (found == faces.end())
    throw DiagnosticError("prototype.face", face, "Prototype references an unknown face label.");
  return faces.at(p.at(static_cast<std::size_t>(found - faces.begin())));
}
Json placement(const std::string &type, const std::vector<std::string> &attachments) {
  Json ports = Json::object();
  std::vector<std::string> occupied;
  if (type == "bandage")
    occupied = {cell_for({attachments[0], attachments[1]}),
                cell_for({attachments[2], attachments[3], attachments[4]})};
  else
    occupied = {cell_for(attachments)};
  for (std::size_t i = 0; i < attachments.size(); ++i)
    ports[std::to_string(i)] = {{"cell", occupied[type == "bandage" && i >= 2 ? 1 : 0]},
                                {"attachment", attachments[i]}};
  return {{"key", type + ":" + joined(attachments, "-")}, {"footprint", occupied}, {"portAttachment", ports}};
}
} // namespace
Permutation compose(const Permutation &a, const Permutation &b) {
  if (a.size() != b.size())
    throw DiagnosticError("symmetry.degree", "/group", "Permutation degrees differ.");
  validate_permutation(a);
  validate_permutation(b);
  Permutation result(a.size());
  for (Index i = 0; i < a.size(); ++i)
    result[i] = a[b[i]];
  return result;
}
Permutation inverse(const Permutation &p) {
  validate_permutation(p);
  Permutation result(p.size());
  for (Index i = 0; i < p.size(); ++i)
    result[p[i]] = i;
  return result;
}
std::vector<Permutation> enumerate_group(const std::vector<Permutation> &generators, std::size_t limit) {
  if (generators.empty() || generators[0].size() > 32 || limit == 0 || limit > 65536)
    throw DiagnosticError("symmetry.limit", "/group",
                          "Supply generators of degree at most 32 and a finite limit at most 65536.");
  for (const auto &g : generators) {
    validate_permutation(g);
    if (g.size() != generators[0].size())
      throw DiagnosticError("symmetry.degree", "/group", "Generator degrees differ.");
  }
  Permutation identity(generators[0].size());
  std::iota(identity.begin(), identity.end(), 0);
  std::set<Permutation> seen{identity};
  std::vector<Permutation> queue{identity};
  for (std::size_t i = 0; i < queue.size(); ++i)
    for (const auto &generator : generators) {
      auto candidate = compose(generator, queue[i]);
      if (seen.insert(candidate).second) {
        if (seen.size() > limit)
          throw DiagnosticError("symmetry.limit", "/group/limit",
                                "Finite group enumeration exceeded its declared limit.");
        queue.push_back(std::move(candidate));
      }
    }
  return {seen.begin(), seen.end()};
}
std::vector<std::vector<Permutation>> enumerate_cosets(const std::vector<Permutation> &group,
                                                       const std::vector<Permutation> &subgroup) {
  const std::set<Permutation> members(group.begin(), group.end());
  if (group.empty() || subgroup.empty())
    throw DiagnosticError("symmetry.subgroup", "/group",
                          "A coset domain requires a nonempty group and subgroup.");
  for (const auto &a : subgroup) {
    if (!members.contains(a))
      throw DiagnosticError("symmetry.subgroup", "/group", "Subgroup element is outside the group.");
    for (const auto &b : subgroup)
      if (std::find(subgroup.begin(), subgroup.end(), compose(a, b)) == subgroup.end())
        throw DiagnosticError("symmetry.subgroup", "/group", "Supplied subgroup is not closed.");
  }
  std::set<Permutation> remaining = members;
  std::vector<std::vector<Permutation>> cosets;
  while (!remaining.empty()) {
    const auto representative = *remaining.begin();
    std::vector<Permutation> coset;
    for (const auto &h : subgroup) {
      auto value = compose(representative, h);
      remaining.erase(value);
      coset.push_back(std::move(value));
    }
    std::sort(coset.begin(), coset.end());
    cosets.push_back(std::move(coset));
  }
  return cosets;
}

Json compile_source(const Json &source) {
  require_exact(source, "/");
  if (source.at("kind") == "finite-definition") {
    const auto definition = load_definition(source);
    auto result = definition->canonical;
    result["definitionDigest"] = definition->digest;
    result["provenance"] = definition->provenance;
    return result;
  }
  if (source.at("kind") != "cube-prototypes" || source.at("schemaVersion") != 1)
    throw DiagnosticError("source.unsupported", "/kind",
                          "Expected cube prototypes or a finite definition, schema version 1.");
  const auto generators = source.at("group").at("generators").get<std::vector<Permutation>>();
  const auto group = enumerate_group(generators, source.at("group").value("limit", 4096U));
  if (group.size() != 24 || group[0].size() != 6)
    throw DiagnosticError("symmetry.cube_group", "/group",
                          "Cube prototypes require the 24-element action on six face labels.");
  Json output{{"kind", "finite-definition"},
              {"schemaVersion", 1},
              {"semanticsVersion", source.at("semanticsVersion")},
              {"puzzleId", source.at("puzzleId")},
              {"ruleModule", "finite-footprint@1"},
              {"goal", {{"kind", "home"}}},
              {"symmetry", {{"members", group}}},
              {"pieceTypes", Json::array()},
              {"pieces", Json::array()},
              {"placementDomains", Json::array()},
              {"operations", Json::array()},
              {"cells", Json::array()}};
  for (const auto &slot : corners)
    output["cells"].push_back(cell_for(slot));
  for (const auto &slot : edges)
    output["cells"].push_back(cell_for(slot));
  for (const auto &face : faces)
    output["cells"].push_back(face);
  Json provenance{{"placements", Json::object()}, {"operations", Json::object()}};
  std::map<std::string, std::vector<std::string>> prototypes;
  for (const auto &type : {"corner", "edge", "center"})
    prototypes[type] = source.at("prototypes").at(type).at("faces").get<std::vector<std::string>>();
  if (prototypes["corner"].size() != 3 || prototypes["edge"].size() != 2 || prototypes["center"].size() != 1)
    throw DiagnosticError("prototype.ports", "/prototypes",
                          "Corner, edge, and center prototypes require three, two, and one ports.");
  output["symmetry"]["positionOrbits"] = Json::object();
  for (const auto &[type, prototype] : prototypes) {
    const auto representative = cell_for(prototype);
    std::vector<Permutation> stabilizer;
    for (const auto &g : group) {
      std::vector<std::string> transported;
      for (const auto &face : prototype)
        transported.push_back(acted(face, g));
      if (cell_for(transported) == representative)
        stabilizer.push_back(g);
    }
    const auto authored = source.at("group").value("subgroupGenerators", Json::object());
    if (authored.contains(type)) {
      const auto supplied = enumerate_group(authored.at(type).get<std::vector<Permutation>>());
      if (supplied != stabilizer)
        throw DiagnosticError("symmetry.stabilizer", "/group/subgroupGenerators/" + type,
                              "Subgroup generators must generate the prototype position's full stabilizer.");
    }
    Json positions = Json::array();
    for (const auto &coset : enumerate_cosets(group, stabilizer)) {
      std::vector<std::string> transported;
      for (const auto &face : prototype)
        transported.push_back(acted(face, coset.front()));
      positions.push_back({{"id", cell_for(transported)}, {"coset", coset}});
    }
    std::sort(positions.begin(), positions.end(), [](const Json &a, const Json &b) {
      return a.at("id").get<std::string>() < b.at("id").get<std::string>();
    });
    output["symmetry"]["positionOrbits"][type] = {
        {"representative", representative}, {"stabilizer", stabilizer}, {"positions", positions}};
  }
  const bool bandaged = source.value("bandage", false);
  if (bandaged)
    prototypes["bandage"] = {"U", "F", "U", "R", "F"};
  for (const auto &[type, prototype] : prototypes) {
    std::map<std::string, Json> placements;
    Json local_ports = Json::array();
    for (std::size_t i = 0; i < prototype.size(); ++i)
      local_ports.push_back(std::to_string(i));
    for (std::size_t i = 0; i < group.size(); ++i) {
      std::vector<std::string> attachments;
      for (const auto &face : prototype)
        attachments.push_back(acted(face, group[i]));
      auto q = placement(type, attachments);
      const auto key = q.at("key").get<std::string>();
      placements.emplace(key, q);
      if (!provenance["placements"].contains(key))
        provenance["placements"][key] = {{"prototype", "/prototypes/" + type}, {"symmetryIndex", i}};
    }
    Json list = Json::array();
    for (const auto &[_, q] : placements)
      list.push_back(q);
    output["placementDomains"].push_back({{"id", type}, {"placements", list}});
    output["pieceTypes"].push_back({{"id", type}, {"placementDomainId", type}, {"localPorts", local_ports}});
  }
  auto add_piece = [&](const std::string &type, const std::vector<std::string> &attachments,
                       const std::string &id) {
    Json labels = Json::object();
    for (std::size_t i = 0; i < attachments.size(); ++i)
      labels[std::to_string(i)] = attachments[i];
    output["pieces"].push_back({{"id", id},
                                {"type", type},
                                {"homePlacement", placement(type, attachments).at("key")},
                                {"portLabels", labels}});
  };
  for (const auto &slot : corners)
    if (!bandaged || cell_for(slot) != "UFR")
      add_piece("corner", slot, "corner/" + cell_for(slot));
  for (const auto &slot : edges)
    if (!bandaged || cell_for(slot) != "UF")
      add_piece("edge", slot, "edge/" + cell_for(slot));
  for (const auto &face : faces)
    add_piece("center", {face}, "center/" + face);
  if (bandaged)
    add_piece("bandage", prototypes["bandage"], "bandage/UF-UFR");
  const auto root = source.at("operationPrototype").at("permutation").get<Permutation>();
  validate_permutation(root);
  if (root.size() != 6)
    throw DiagnosticError("prototype.operation", "/operationPrototype",
                          "Operation prototype must act on six labels.");
  const auto support = source.at("operationPrototype").at("supportFace").get<std::string>();
  std::map<std::string, Permutation> operations;
  for (std::size_t i = 0; i < group.size(); ++i) {
    const auto face = acted(support, group[i]);
    const auto p = compose(group[i], compose(root, inverse(group[i])));
    if (operations.contains(face) && operations.at(face) != p)
      throw DiagnosticError("prototype.stabilizer", "/operationPrototype",
                            "Operation and guard do not share the declared family stabilizer.");
    operations[face] = p;
    provenance["operations"][face] = {{"prototype", "/operationPrototype"}, {"symmetryIndex", i}};
  }
  for (const auto &[face, forward] : operations)
    for (bool reversed : {false, true}) {
      const auto p = reversed ? inverse(forward) : forward;
      const auto id = face + (reversed ? "'" : "");
      Json selected = Json::array();
      for (const auto &cell : output["cells"])
        if (cell.get<std::string>().find(face) != std::string::npos)
          selected.push_back(cell);
      Json transports = Json::object();
      for (const auto &domain : output["placementDomains"]) {
        const auto type = domain.at("id").get<std::string>();
        Json map = Json::object();
        for (const auto &q : domain.at("placements")) {
          std::vector<std::string> attachments;
          for (const auto &[_, port] : q.at("portAttachment").items())
            attachments.push_back(acted(port.at("attachment").get<std::string>(), p));
          map[q.at("key").get<std::string>()] = placement(type, attachments).at("key");
        }
        transports[type] = map;
      }
      output["operations"].push_back({{"id", id},
                                      {"inverse", face + (reversed ? "" : "'")},
                                      {"family", face},
                                      {"transport", face + (reversed ? ".counterclockwise" : ".clockwise")},
                                      {"selectedCells", selected},
                                      {"transports", transports},
                                      {"facePermutation", p}});
      provenance["operations"][id] = provenance["operations"][face];
    }
  Json initial = Json::object();
  for (const auto &piece : output["pieces"])
    initial[piece.at("id").get<std::string>()] = piece.at("homePlacement");
  output["initialState"] = {{"placementOf", initial}, {"mechanism", Json::object()}};
  output["provenance"] = provenance;
  const auto definition = load_definition(output);
  auto result = definition->canonical;
  result["definitionDigest"] = definition->digest;
  result["provenance"] = provenance;
  return result;
}
Json compile_json(const std::string &source) {
  try {
    if (source.size() > 4 * 1024 * 1024)
      throw DiagnosticError("document.limit", "/", "Definition import is limited to 4 MiB.");
    return {{"status", "Compiled"},
            {"definition", compile_source(Json::parse(source))},
            {"diagnostics", Json::array()}};
  } catch (const std::exception &error) {
    return diagnostic_result(error);
  }
}
} // namespace twisty
