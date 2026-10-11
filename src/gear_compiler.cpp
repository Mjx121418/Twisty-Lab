#include "twisty/compiler.hpp"
#include <algorithm>
#include <set>

namespace twisty {
namespace {
const std::vector<std::string> faces{"U", "R", "F", "D", "L", "B"};
std::string opposite(const std::string &face) {
  return faces[(std::find(faces.begin(), faces.end(), face) - faces.begin() + 3) % 6];
}
std::string gear_key(const std::string &base, int phase) {
  return "gear-" + base + "/p" + std::to_string(phase);
}
} // namespace

Json compile_gear_source(const Json &source) {
  require_exact(source, "/");
  if (source.at("schemaVersion") != 1 || !source.at("gearedEdges").is_array())
    throw DiagnosticError("gear.source", "/", "Expected gear-cube-prototypes version 1 and gearedEdges.");
  // These are abstract permutations of face labels, not spatial matrices.
  Json cube{{"schemaVersion", 1},
            {"semanticsVersion", source.at("semanticsVersion")},
            {"kind", "cube-prototypes"},
            {"puzzleId", source.at("puzzleId")},
            {"bandage", false},
            {"group", {{"generators", {{0, 2, 4, 3, 5, 1}, {5, 1, 0, 2, 4, 3}}}, {"limit", 24}}},
            {"prototypes",
             {{"corner", {{"faces", {"U", "R", "F"}}}},
              {"edge", {{"faces", {"U", "F"}}}},
              {"center", {{"faces", {"U"}}}}}},
            {"operationPrototype", {{"supportFace", "U"}, {"permutation", {0, 2, 4, 3, 5, 1}}}}};
  auto base = compile_source(cube);
  std::set<std::string> available, geared;
  for (const auto &piece : base.at("pieces"))
    if (piece.at("type") == "edge")
      available.insert(piece.at("id").get<std::string>().substr(5));
  for (const auto &edge : source.at("gearedEdges")) {
    if (!edge.is_string() || !available.contains(edge.get<std::string>()) ||
        !geared.insert(edge.get<std::string>()).second)
      throw DiagnosticError("gear.edge", "/gearedEdges", "Name distinct cube edge IDs, e.g. UF or FR.");
  }
  auto output = base;
  output.erase("definitionDigest");
  output["ruleModule"] = "finite-placement-relations@2";
  output["operations"] = Json::array();
  Json gear_domain{{"id", "gear-edge"}, {"placements", Json::array()}};
  for (const auto &domain : base.at("placementDomains"))
    if (domain.at("id") == "edge")
      for (const auto &q : domain.at("placements"))
        for (int phase = 0; phase < 6; ++phase) {
          auto placement = q;
          placement["key"] = gear_key(q.at("key"), phase);
          for (auto &[_, port] : placement["portAttachment"].items())
            port["attachment"] = port.at("attachment").get<std::string>() + "/p" + std::to_string(phase);
          gear_domain["placements"].push_back(std::move(placement));
        }
  output["placementDomains"].push_back(gear_domain);
  output["pieceTypes"].push_back(
      {{"id", "gear-edge"}, {"placementDomainId", "gear-edge"}, {"localPorts", {"0", "1"}}});
  for (auto &piece : output["pieces"])
    if (piece.at("type") == "edge" && geared.contains(piece.at("id").get<std::string>().substr(5))) {
      piece["type"] = "gear-edge";
      piece["homePlacement"] = gear_key(piece.at("homePlacement"), 0);
    }
  std::map<std::string, Json> quarter;
  for (const auto &op : base.at("operations"))
    quarter[op.at("id")] = op;
  for (const auto &face : faces)
    for (bool ordinary : {false, true})
      for (bool reversed : {false, true}) {
        const auto id = face + (ordinary ? "q" : "") + (reversed ? "'" : "");
        const auto inverse_id = face + (ordinary ? "q" : "") + (reversed ? "" : "'");
        const auto &qop = quarter.at(face + (reversed ? "'" : ""));
        const auto &opposite_qop = quarter.at(face + (reversed ? "" : "'"));
        Json rules = Json::object(), geared_middle = Json::array();
        for (const auto &domain : output.at("placementDomains")) {
          const auto type = domain.at("id").get<std::string>();
          const auto base_type = type == "gear-edge" ? "edge" : type;
          Json blocked = Json::array(), transports = Json::object();
          for (const auto &placement : domain.at("placements")) {
            const auto key = placement.at("key").get<std::string>();
            const auto cell = placement.at("footprint").at(0).get<std::string>();
            const bool outer = cell.find(face) != std::string::npos;
            const bool opposite_layer = cell.find(opposite(face)) != std::string::npos;
            const bool middle = !outer && !opposite_layer;
            if (middle && type == "gear-edge")
              geared_middle.push_back(key);
            if (middle && ordinary && type == "gear-edge") {
              blocked.push_back(key);
              continue;
            }
            if ((ordinary && !outer) || (!ordinary && middle && type != "gear-edge"))
              continue;
            auto from = type == "gear-edge" ? key.substr(5, key.size() - 8) : key;
            auto to = from;
            if (outer || opposite_layer)
              to = (outer ? qop : opposite_qop).at("transports").at(base_type).at(from).get<std::string>();
            if (type == "gear-edge") {
              int phase = key.back() - '0';
              if (middle) {
                const auto &ports = placement.at("portAttachment");
                const auto first = ports.at("0").at("attachment").get<std::string>().substr(0, 1);
                const auto second = ports.at("1").at("attachment").get<std::string>().substr(0, 1);
                const auto &group = base.at("symmetry").at("members");
                const auto frame = std::find_if(group.begin(), group.end(), [&](const Json &p) {
                  return faces[p.at(0).get<int>()] == first && faces[p.at(2).get<int>()] == second;
                });
                const int sign = faces[frame->at(1).get<int>()] == face ? 1 : -1;
                phase = (phase + (reversed ? -sign : sign) + 6) % 6;
              }
              to = gear_key(to, phase);
            }
            transports[key] = to;
          }
          rules[type] = {{"blocked", blocked}, {"transports", transports}};
        }
        Json operation{{"id", id},
                       {"inverse", inverse_id},
                       {"family", face},
                       {"transport", id + ".coupled"},
                       {"placementRules", rules}};
        if (!ordinary)
          operation["placementCountGuards"] = {{{"id", "geared-middle"},
                                                {"placementDomainId", "gear-edge"},
                                                {"placements", geared_middle},
                                                {"min", 1},
                                                {"max", 4}}};
        output["operations"].push_back(std::move(operation));
      }
  Json initial = Json::object();
  for (const auto &piece : output.at("pieces"))
    initial[piece.at("id").get<std::string>()] = piece.at("homePlacement");
  output["initialState"] = {{"placementOf", initial}, {"mechanism", Json::object()}};
  output["provenance"] = {{"authoringKind", "gear-cube-prototypes"},
                          {"gearedEdges", source.at("gearedEdges")},
                          {"modelStatus", "experimental"},
                          {"couplingPolicy", "any-geared-middle-slice-v2"}};
  const auto definition = load_definition(output);
  output = definition->canonical;
  output["definitionDigest"] = definition->digest;
  output["provenance"] = definition->provenance;
  return output;
}
} // namespace twisty
