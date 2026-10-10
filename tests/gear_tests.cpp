#include "twisty/kernel.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
using namespace twisty;
namespace {
std::string read(const std::string &path) {
  std::ifstream f(std::string(PROJECT_ROOT) + "/" + path);
  std::ostringstream out;
  out << f.rdbuf();
  if (!f)
    throw std::runtime_error(path);
  return out.str();
}
void check(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
Json move(Session &s, const std::string &op) {
  return s.execute(op, s.revision());
}
bool close(const std::vector<float> &a, const std::vector<float> &b, double tolerance = 1e-5) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::abs(a[i] - b[i]) > tolerance)
      return false;
  return true;
}
void models() {
  Session standard(read("packages/gear/source.json"));
  auto origin = standard.snapshot();
  check(standard.definition()->pieces.size() == 26,
        "One persistent edge per position, no duplicate carriers");
  check(move(standard, "Rq").at("reasonCode") == "placement.blocked",
        "Geared middle slice blocks uncoupled quarter turn");
  check(standard.snapshot() == origin, "Blocked move is pure");
  for (const auto &face : {"U", "R", "F", "D", "L", "B"}) {
    check(standard.run(std::string(face) + "12", "transactional", standard.revision()).at("status") ==
              "Committed",
          "Twelve gear turns legal");
    check(standard.snapshot().at("stateDigest") == origin.at("stateDigest"),
          "Order twelve restores carrier, cog and labels");
  }
  auto turn = move(standard, "R");
  check(turn.at("status") == "Committed", "Gear R legal");
  const auto &actions = turn.at("transitions").at(0).at("pieceActions");
  check(
      std::any_of(actions.begin(), actions.end(),
                  [](const Json &a) { return a.at("pieceId") == "center/R" && a.at("from") == a.at("to"); }),
      "Unchanged center still participates");
  check(standard.run("U F L D R B (U F L D R B)' R'", "transactional", standard.revision()).at("status") ==
            "Committed",
        "Mixed-axis inverse sequence");
  check(standard.snapshot().at("stateDigest") == origin.at("stateDigest"),
        "Full inverse restores exact state");
  Session restored(read("packages/gear/source.json"));
  check(restored.load(standard.save()).at("status") == "Loaded", "Gear replay loads");
  Session extreme(read("packages/gear-extreme/source.json"));
  check(move(extreme, "U").at("reasonCode") == "placement.blocked", "Plain middle slice blocks geared U");
  check(move(extreme, "Uq").at("status") == "Committed", "Extreme permits ordinary U");
  check(extreme.run("R F Uq Dq (R F Uq Dq)' Uq'", "transactional", extreme.revision()).at("status") ==
            "Committed",
        "Extreme coupling and inverse");
  check(extreme.snapshot().at("solved") == true, "Extreme restored");
  Session plain(read("packages/gear-plain/source.json"));
  check(plain.run("Rq Uq Fq (Rq Uq Fq)'", "transactional", plain.revision()).at("status") == "Committed",
        "All-plain family has ordinary turns");
  auto source = Json::parse(read("packages/gear/source.json"));
  source["gearedEdges"].erase(0);
  Session mixed(source.dump());
  check(move(mixed, "R").at("reasonCode") == "placement.blocked" &&
            move(mixed, "Rq").at("reasonCode") == "placement.blocked",
        "Mixed slice blocks incompatible transports");
  check(move(mixed, "U").at("status") == "Committed", "Other uniform slices remain usable");
  auto state = Json::parse(mixed.state_json());
  state["placementOf"]["edge/UR"] = "gear-" + state["placementOf"]["edge/UF"].get<std::string>() + "/p0";
  check(mixed.validate_state_json(state.dump()).find("state.occupancy") != std::string::npos,
        "Overlapping edge placements rejected");
}
void animation(const std::string &package, const std::string &algorithm,
               const std::string &variant = "") {
  Session s(read("packages/" + package + "/source.json"));
  for (const auto &kind : {"euclidean", "port-diagram"}) {
    Geometry g(s.definition(), read("packages/" + package + "/" + package + variant + "-" + kind + ".json"));
    const auto scene = Json::parse(g.scene_json());
    const auto initial = g.transforms();
    const auto result = s.run(algorithm, "transactional", s.revision());
    check(result.at("status") == "Committed", "Animation sequence legal");
    for (const auto &record : result.at("transitions")) {
      check(Json::parse(g.prepare_animation_json(record.dump())).at("status") == "Prepared",
            "Prepare recorded transport");
      const auto source = g.transforms();
      g.sample(.5);
      const auto middle = g.transforms();
      check(!close(middle, source), "Intermediate frame moves");
      g.sample(1 - 1e-8);
      const auto near = g.transforms();
      g.sample(1);
      const auto end = g.transforms();
      // Quotiented featureless centers may change frame by a declared mesh symmetry.
      for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i)
        if (!scene.at("visualParts")[i].at("pieceId").get<std::string>().starts_with("center/"))
          for (int k = 0; k < 16; ++k)
            check(std::abs(near[i * 16 + k] - end[i * 16 + k]) < 1e-5, "No cog or carrier endpoint snap");
      g.set_state_json(record.at("afterState").dump());
      check(close(end, g.transforms()), "Endpoint matches direct state realization");
      for (const auto &part : scene.at("visualParts"))
        check(Json::parse(g.bind_hit_json(part.at("visualPartId"))).at("pieceId") == part.at("pieceId"),
              "Components bind to shared piece identity");
    }
    check(close(initial, g.transforms()), "Inverse restores all resting frames");
  }
}
void invalid_geometry() {
  Session s(read("packages/gear/source.json"));
  auto r = Json::parse(read("packages/gear/gear-euclidean.json"));
  r["componentsByDomain"]["gear-edge"][1]["operationTracks"]["R"]["gear-edge:U-F/p0"][1]["angle"] = .25;
  bool rejected = false;
  try {
    Geometry bad(s.definition(), r.dump());
  } catch (const DiagnosticError &e) {
    rejected = e.code == "realization.articulated";
  }
  check(rejected, "Wrong cog ratio rejected at registration");
  r = Json::parse(read("packages/gear/gear-euclidean.json"));
  r["assets"]["gear-edge/cog"]["triangles"][0][0] = .5;
  rejected = false;
  try {
    Geometry bad(s.definition(), r.dump());
  } catch (const DiagnosticError &e) {
    rejected = e.code == "realization.articulated";
  }
  check(rejected, "Fractional mesh index rejected");
  for (const auto &field : {"vertex", "frame", "pivot"}) {
    r = Json::parse(read("packages/gear/gear-euclidean.json"));
    if (std::string(field) == "vertex")
      r["assets"]["gear-edge/cog"]["vertices"][0][0] = 1e100;
    else if (std::string(field) == "frame")
      r["componentsByDomain"]["gear-edge"][0]["placementFrames"]["gear-edge:U-F/p0"][12] = 1e100;
    else
      r["componentsByDomain"]["gear-edge"][0]["operationTracks"]["R"]["gear-edge:U-F/p0"][0]["pivot"][0] =
          1e100;
    rejected = false;
    try {
      Geometry bad(s.definition(), r.dump());
    } catch (const DiagnosticError &e) {
      rejected = e.code == "realization.articulated";
    }
    check(rejected, "Finite double inputs must not overflow Float32 buffers");
  }
}
void coupled_midpoint(bool six_teeth = false) {
  Session s(read("packages/gear/source.json"));
  Geometry g(s.definition(), read(six_teeth ? "packages/gear/gear-six-teeth-euclidean.json"
                                         : "packages/gear/gear-euclidean.json"));
  const auto scene = Json::parse(g.scene_json());
  const auto transition = move(s, "R").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(transition.dump())).at("status") == "Prepared",
        "Prepare coupled R");
  g.sample(.5);
  auto matrix = [&](const std::string &id, int entry) {
    const auto &parts = scene.at("visualParts");
    for (std::size_t i = 0; i < parts.size(); ++i)
      if (parts[i].at("visualPartId") == id)
        return g.transforms()[i * 16 + entry];
    throw std::runtime_error(id);
  };
  // At shared progress 1/2: the carrier turns 45 degrees, the outer layer
  // turns 90 degrees, and the cog spins 30 or 150 degrees in its moving frame.
  check(std::abs(matrix("edge/UF/carrier", 5) - std::sqrt(.5)) < 1e-6,
        "Middle carrier uses half the outer angle");
  check(std::abs(matrix("corner/UFR/body", 5)) < 1e-6, "Outer corner uses its own rotation angle");
  const double x = six_teeth ? -std::sqrt(.75) : std::sqrt(.75);
  const double z = -.5;
  check(std::abs(matrix("edge/UF/cog", 0) - x) < 1e-6 &&
            std::abs(matrix("edge/UF/cog", 1)) < 1e-6 && std::abs(matrix("edge/UF/cog", 2) - z) < 1e-6,
        "Cog combines carrier rotation and local spin rather than sharing the carrier frame");
  g.set_state_json(s.state_json());
  const auto inverse = move(s, "R'").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(inverse.dump())).at("status") == "Prepared",
        "Prepare inverse coupled R");
  g.sample(.5);
  check(std::abs(matrix("edge/UF/cog", 0) - x) < 1e-6 &&
            std::abs(matrix("edge/UF/cog", 1)) < 1e-6 && std::abs(matrix("edge/UF/cog", 2) - z) < 1e-6,
        "Inverse retraces the signed cog rotation through its forward midpoint");
}
void fixed_pivot() {
  Json q0{{"key", "a"},
          {"footprint", {"slot"}},
          {"portAttachment", {{"p", {{"cell", "slot"}, {"attachment", "a"}}}}}};
  auto q1 = q0;
  q1["key"] = "b";
  q1["portAttachment"]["p"]["attachment"] = "b";
  Json source{
      {"schemaVersion", 1},
      {"semanticsVersion", "1.0.0"},
      {"kind", "finite-definition"},
      {"puzzleId", "pivot-test"},
      {"ruleModule", "finite-placement-relations@1"},
      {"cells", {"slot"}},
      {"goal", {{"kind", "home"}}},
      {"pieceTypes", {{{"id", "part"}, {"placementDomainId", "part"}, {"localPorts", {"p"}}}}},
      {"placementDomains", {{{"id", "part"}, {"placements", {q0, q1}}}}},
      {"pieces", {{{"id", "piece"}, {"type", "part"}, {"homePlacement", "a"}, {"portLabels", {{"p", "U"}}}}}},
      {"initialState", {{"placementOf", {{"piece", "a"}}}, {"mechanism", Json::object()}}},
      {"operations", Json::array()}};
  for (bool inverse : {false, true}) {
    Json rule{{"blocked", Json::array()}, {"transports", {{"a", "b"}, {"b", "a"}}}};
    source["operations"].push_back({{"id", inverse ? "T'" : "T"},
                                    {"inverse", inverse ? "T" : "T'"},
                                    {"family", "T"},
                                    {"transport", "rotation"},
                                    {"placementRules", {{"part", rule}}}});
  }
  Session s(source.dump());
  std::vector<double> a{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1},
      b{0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1};
  Json step{{"axis", {0, 0, 1}}, {"pivot", {1, 0, 0}}, {"angle", std::numbers::pi / 2}, {"space", "world"}};
  auto inverse = step;
  inverse["angle"] = -std::numbers::pi / 2;
  Json component{{"id", "marker"},
                 {"role", "port"},
                 {"portId", "p"},
                 {"meshAssetId", "triangle"},
                 {"placementFrames", {{"a", a}, {"b", b}}},
                 {"operationTracks",
                  {{"T", {{"a", {step}}, {"b", {inverse}}}}, {"T'", {{"a", {step}}, {"b", {inverse}}}}}}};
  Json r{{"schemaVersion", 1},
         {"id", "pivot-realization"},
         {"kind", "articulated-euclidean"},
         {"compatibleDefinitionDigest", s.definition()->digest},
         {"requiredCapabilities", {"triangle-meshes", "rigid-transforms"}},
         {"assets",
          {{"triangle", {{"vertices", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, {"triangles", {{0, 1, 2}}}}}}},
         {"componentsByDomain", {{"part", {component}}}}};
  Geometry g(s.definition(), r.dump());
  const auto transition = move(s, "T").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(transition.dump())).at("status") == "Prepared",
        "Prepare fixed pivot");
  g.sample(.5);
  check(std::abs(g.transforms()[12] - (1 + std::sqrt(.5))) < 1e-6 &&
            std::abs(g.transforms()[13] - std::sqrt(.5)) < 1e-6,
        "World pivot follows circular arc rather than endpoint interpolation");
}
} // namespace
int main() {
  try {
    models();
    animation("gear", "R U F (R U F)'");
    animation("gear-extreme", "Uq R F (Uq R F)'");
    animation("gear", "R U F (R U F)' R12", "-six-teeth");
    animation("gear-extreme", "Uq R F (Uq R F)'", "-six-teeth");
    invalid_geometry();
    coupled_midpoint();
    coupled_midpoint(true);
    fixed_pivot();
    std::cout << "Gear rules and articulated motions verified\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
