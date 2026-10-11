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
  check(move(extreme, "U").at("reasonCode") == "placement.count", "Empty geared slice selects ordinary U");
  check(move(extreme, "Uq").at("status") == "Committed", "Extreme permits ordinary U");
  check(extreme.run("R Fq (R Fq)' Uq Dq (Uq Dq)' Uq'", "transactional", extreme.revision()).at("status") ==
            "Committed",
        "Extreme coupling and inverse");
  check(extreme.snapshot().at("solved") == true, "Extreme restored");
  Session plain(read("packages/gear-plain/source.json"));
  check(plain.run("Rq Uq Fq (Rq Uq Fq)'", "transactional", plain.revision()).at("status") == "Committed",
        "All-plain family has ordinary turns");
  auto source = Json::parse(read("packages/gear/source.json"));
  source["gearedEdges"].erase(0);
  Session mixed(source.dump());
  check(move(mixed, "Rq").at("reasonCode") == "placement.blocked",
        "Any geared middle edge blocks the ordinary request");
  check(move(mixed, "R").at("status") == "Committed", "Mixed slice permits the coupled request");
  check(move(mixed, "U").at("status") == "Committed", "Other geared slices remain usable");
  auto state = Json::parse(mixed.state_json());
  state["placementOf"]["edge/UR"] = "gear-" + state["placementOf"]["edge/UF"].get<std::string>() + "/p0";
  check(mixed.validate_state_json(state.dump()).find("state.occupancy") != std::string::npos,
        "Overlapping edge placements rejected");
}
void custom_coupling() {
  const std::vector<std::string> faces{"U", "R", "F", "D", "L", "B"};
  auto source = Json::parse(read("packages/gear/source.json"));
  // Cover zero through four gears in a slice, gears in outer layers, and the
  // user's adjacent UF/UR mask. Membership is always read from the current state.
  std::vector<Json> masks{Json::array({"UF", "UR"})};
  const std::vector<std::string> middle{"UF", "UB", "DF", "DB"};
  for (int bits = 0; bits < 16; ++bits) {
    Json mask = Json::array();
    for (int i = 0; i < 4; ++i)
      if (bits & (1 << i))
        mask.push_back(middle[i]);
    masks.push_back(mask);
    mask.push_back("UR");
    masks.push_back(mask);
  }
  for (const auto &mask : masks) {
    source["gearedEdges"] = mask;
    Session s(source.dump());
    auto operation = [&](std::size_t axis) {
      bool geared = false;
      for (Index i = 0; i < s.definition()->pieces.size(); ++i) {
        const auto &piece = s.definition()->pieces[i];
        const auto &domain = s.definition()->domains[piece.domain];
        if (domain.id != "gear-edge")
          continue;
        const auto &cell = domain.placements[s.state().placement_of[i]].footprint[0];
        if (cell.find(faces[axis]) == std::string::npos &&
            cell.find(faces[(axis + 3) % 6]) == std::string::npos)
          geared = true;
      }
      const auto before = s.snapshot();
      check(plan(*s.definition(), s.state(), faces[axis]).transition.has_value() == geared,
            "Coupled request iff at least one current middle edge is geared");
      check(plan(*s.definition(), s.state(), faces[axis] + "q").transition.has_value() == !geared,
            "Ordinary request iff no current middle edge is geared");
      check(s.snapshot() == before, "Conditional planning preserves state and history");
      return faces[axis] + (geared ? "" : "q");
    };
    for (std::size_t axis = 0; axis < faces.size(); ++axis) {
      const auto before = s.state();
      const auto op = operation(axis);
      check(move(s, op).at("status") == "Committed", "Every axis permits its selected turn");
      check(move(s, op + "'").at("status") == "Committed" && s.state() == before,
            "Selected turn has a legal exact inverse");
    }
    std::vector<std::string> walk;
    for (int step = 0; step < 18; ++step) {
      for (std::size_t axis = 0; axis < faces.size(); ++axis)
        operation(axis);
      const auto op = operation(static_cast<std::size_t>(step % 6));
      const auto before = s.state();
      const auto result = move(s, op);
      check(result.at("status") == "Committed", "Dynamic custom-mask walk stays legal");
      for (Index i = 0; i < s.definition()->pieces.size(); ++i) {
        const auto &piece = s.definition()->pieces[i];
        const auto &domain = s.definition()->domains[piece.domain];
        if (domain.id != "gear-edge")
          continue;
        const auto &from = domain.placements[before.placement_of[i]];
        const auto &to = domain.placements[s.state().placement_of[i]];
        const auto axis = static_cast<std::size_t>(step % 6);
        const auto &cell = from.footprint[0];
        const bool middle = cell.find(faces[axis]) == std::string::npos &&
                            cell.find(faces[(axis + 3) % 6]) == std::string::npos;
        check((from.key.back() != to.key.back()) == (middle && op.find('q') == std::string::npos),
              "Only geared middle edges receive local cog spin");
      }
      walk.push_back(op);
    }
    for (auto it = walk.rbegin(); it != walk.rend(); ++it)
      check(move(s, *it + "'").at("status") == "Committed", "Dynamic coupling walk retraces");
    check(s.snapshot().at("solved") == true, "Custom-mask inverse walk restores the home state");
  }
}
void invalid_count_guards() {
  auto compiled = Json::parse(read("packages/gear/definition.json"));
  compiled.erase("definitionDigest");
  auto invalid = [&](Json document, const std::string &reason) {
    bool rejected = false;
    try {
      load_definition(std::move(document));
    } catch (const DiagnosticError &e) {
      rejected = e.code == reason;
    }
    check(rejected, "Invalid count guards fail with " + reason);
  };
  auto bad = compiled;
  bad["ruleModule"] = "finite-placement-relations@1";
  invalid(bad, "rule.fields");
  auto &operations = compiled["operations"];
  const auto index =
      std::find_if(operations.begin(), operations.end(), [](const Json &op) { return op.at("id") == "R"; }) -
      operations.begin();
  for (const auto &field : {"domain", "placement", "duplicate", "bounds", "fraction"}) {
    bad = compiled;
    auto &guard = bad["operations"][index]["placementCountGuards"][0];
    if (std::string(field) == "domain")
      guard["placementDomainId"] = "missing";
    else if (std::string(field) == "placement")
      guard["placements"][0] = "missing";
    else if (std::string(field) == "duplicate")
      guard["placements"].push_back(guard["placements"][0]);
    else if (std::string(field) == "bounds")
      guard["min"] = 5;
    else
      guard["min"] = .5;
    invalid(bad, std::string(field) == "fraction" ? "value.inexact" : "guard.count");
  }
  bad = compiled;
  bad["operations"][index]["placementCountGuards"][0]["min"] = 2;
  invalid(bad, "inverse.count_guard");
  bad = compiled;
  bad["operations"][index]["placementCountGuards"][0]["placements"].erase(0);
  invalid(bad, "inverse.count_guard");
  auto reordered = compiled;
  for (auto &op : reordered["operations"])
    if (op.contains("placementCountGuards"))
      std::reverse(op["placementCountGuards"][0]["placements"].begin(),
                   op["placementCountGuards"][0]["placements"].end());
  check(load_definition(reordered)->digest == load_definition(compiled)->digest,
        "Guard placement ordering does not change the semantic digest");
  auto bounded = compiled;
  for (auto &op : bounded["operations"])
    if (op.at("id") == "R" || op.at("id") == "R'") {
      op["placementCountGuards"][0]["min"] = 0;
      op["placementCountGuards"][0]["max"] = 2;
    }
  const auto definition = load_definition(bounded);
  const auto blocked = plan(*definition, definition->initial, "R");
  check(!blocked.transition && blocked.blocked.at("reasonCode") == "placement.count" &&
            blocked.blocked.at("evidence").at("count") == 4 &&
            blocked.blocked.at("implicatedPieces").size() == 4,
        "Inclusive upper bounds report the matching pieces and exact count");
}
void animation(const std::string &package, const std::string &algorithm, const std::string &variant = "",
               bool include_diagram = true) {
  Session s(read("packages/" + package + "/source.json"));
  std::vector<std::string> kinds{"euclidean"};
  if (include_diagram)
    kinds.push_back("port-diagram");
  for (const auto &kind : kinds) {
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
  r["componentsByDomain"]["gear-edge"][1]["operationTracks"]["R"]["gear-edge:U-F/p0"][0]["angle"] = .25;
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
      r["componentsByDomain"]["gear-edge"][1]["operationTracks"]["R"]["gear-edge:U-F/p0"][0]["pivot"][0] =
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
void coupled_midpoint(bool six_teeth = false, bool imported = false) {
  Session s(read("packages/gear/source.json"));
  Geometry g(s.definition(), read(imported    ? "packages/gear/gear-normal-euclidean.json"
                                  : six_teeth ? "packages/gear/gear-six-teeth-euclidean.json"
                                              : "packages/gear/gear-euclidean.json"));
  const auto scene = Json::parse(g.scene_json());
  const auto initial = g.transforms();
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
  // At shared progress 1/2: the middle carrier stays fixed, the opposite
  // outer layers turn +/-45 degrees, and the cog spins 30 or 150 degrees.
  check(std::abs(matrix("edge/UF/carrier", 5) - 1) < 1e-6, "Middle carrier stays fixed");
  check(std::abs(matrix("corner/UFR/body", 5) - std::sqrt(.5)) < 1e-6 &&
            std::abs(matrix("corner/UFR/body", 6) + std::sqrt(.5)) < 1e-6,
        "Requested outer face turns clockwise");
  // The opposite corner has a different home frame. Remove that frame to
  // compare its world rotation with the requested face's motion.
  const auto &parts = scene.at("visualParts");
  for (std::size_t i = 0; i < parts.size(); ++i)
    if (parts[i].at("visualPartId") == "corner/UFL/body") {
      const auto &m = g.transforms();
      double y = 0, z = 0;
      for (int k = 0; k < 3; ++k) {
        y += m[i * 16 + k * 4 + 1] * initial[i * 16 + k * 4 + 1];
        z += m[i * 16 + k * 4 + 2] * initial[i * 16 + k * 4 + 1];
      }
      check(std::abs(y - std::sqrt(.5)) < 1e-6 && std::abs(z - std::sqrt(.5)) < 1e-6,
            "Opposite outer face turns in the reverse world direction");
    }
  const double x = six_teeth ? -std::sqrt(.75) : std::sqrt(.75);
  const double y = std::sqrt(.125);
  const double z = -std::sqrt(.125);
  check(std::abs(matrix("edge/UF/cog", 0) - x) < 1e-6 && std::abs(matrix("edge/UF/cog", 1) - y) < 1e-6 &&
            std::abs(matrix("edge/UF/cog", 2) - z) < 1e-6,
        "Cog spins in its stationary middle-layer carrier frame");
  g.set_state_json(s.state_json());
  const auto inverse = move(s, "R'").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(inverse.dump())).at("status") == "Prepared",
        "Prepare inverse coupled R");
  g.sample(.5);
  check(std::abs(matrix("edge/UF/cog", 0) - x) < 1e-6 && std::abs(matrix("edge/UF/cog", 1) - y) < 1e-6 &&
            std::abs(matrix("edge/UF/cog", 2) - z) < 1e-6,
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
void supplied_model() {
  Session s(read("packages/gear/source.json"));
  auto r = Json::parse(read("packages/gear/gear-normal-euclidean.json"));
  Geometry g(s.definition(), r.dump());
  const auto scene = Json::parse(g.scene_json());
  int bodies = 0, balls = 0, edge_patches = 0;
  for (const auto &part : scene.at("visualParts")) {
    bodies += part.at("role") == "body";
    if (part.at("visualPartId") == "center/U/core-ball")
      ++balls;
    if (part.at("pieceId").get<std::string>().starts_with("edge/") && part.at("role") == "port")
      ++edge_patches;
  }
  check(bodies == 39 && balls == 1 && edge_patches == 48,
        "Imported model has 38 exterior bodies, one core and two patches per edge port");
  const auto &parts = scene.at("visualParts");
  auto ball_index =
      std::find_if(parts.begin(), parts.end(),
                   [](const Json &part) { return part.at("visualPartId") == "center/U/core-ball"; }) -
      parts.begin();
  const auto initial = g.transforms();
  const auto turn = move(s, "U").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(turn.dump())).at("status") == "Prepared",
        "Prepare imported gear motion");
  g.sample(.5);
  for (int i = 0; i < 16; ++i)
    check(g.transforms()[ball_index * 16 + i] == initial[ball_index * 16 + i],
          "Internal ball stays fixed during the bound center's turn");
  for (const auto &filter : {Json::array(), Json::array({"missing"}), Json::array({"corner/UFR"}),
                             Json::array({"center/U", "center/U"})}) {
    auto bad = r;
    bad["componentsByDomain"]["center"].back()["pieceIds"] = filter;
    bool rejected = false;
    try {
      Geometry invalid(s.definition(), bad.dump());
    } catch (const DiagnosticError &e) {
      rejected = e.code == "realization.articulated";
    }
    check(rejected, "Body filters must identify distinct existing pieces in their domain");
  }
  auto bad = r;
  auto &components = bad["componentsByDomain"]["gear-edge"];
  components.erase(
      std::remove_if(components.begin(), components.end(),
                     [](const Json &part) { return part.value("portId", std::string{}) == "0"; }),
      components.end());
  bool rejected = false;
  try {
    Geometry invalid(s.definition(), bad.dump());
  } catch (const DiagnosticError &e) {
    rejected = e.code == "realization.articulated";
  }
  check(rejected, "Split patches still require complete local-port coverage");
}
void supplied_plain_edges() {
  const auto meshes = Json::parse(read("packages/gear-extreme/plain-edge-meshes.json"));
  for (const auto &package : {"gear-extreme", "gear-plain"}) {
    Session s(read("packages/" + std::string(package) + "/source.json"));
    const auto r =
        Json::parse(read("packages/" + std::string(package) + "/" + package + "-supplied-euclidean.json"));
    check(r.at("assets").at("edge/body") == meshes.at("assets").at("edge/body"),
          "Supplied variants retain the imported plain-edge triangles");
    Geometry g(s.definition(), r.dump());
    const auto scene = Json::parse(g.scene_json());
    int bodies = 0;
    for (const auto &part : scene.at("visualParts"))
      bodies += part.at("role") == "body";
    check(bodies == (std::string(package) == "gear-extreme" ? 35 : 27),
          "A plain edge replaces the entire two-body geared assembly");
    for (const auto &piece : s.definition()->pieces)
      if (s.definition()->domains[piece.domain].id == "edge") {
        int components = 0;
        for (const auto &part : scene.at("visualParts"))
          if (part.at("pieceId").get<std::string>() == piece.id) {
            ++components;
            check(part.at("meshAssetId").get<std::string>().starts_with("edge/"),
                  "Plain edges never instantiate carriers or cogs");
          }
        check(components == 3, "One rigid body and two labeled patches per plain edge");
      }
  }
  auto source = Json::parse(read("packages/gear/source.json"));
  source["gearedEdges"].erase(0); // UF becomes a plain piece away from the Extreme preset's plane.
  Session custom(source.dump());
  auto r = Json::parse(read("packages/gear/gear-supplied-euclidean.json"));
  r["compatibleDefinitionDigest"] = custom.definition()->digest;
  Geometry g(custom.definition(), r.dump());
  const auto initial = g.transforms();
  for (const auto &op : {"R", "R'"}) {
    const auto record = move(custom, op).at("transitions").at(0);
    check(Json::parse(g.prepare_animation_json(record.dump())).at("status") == "Prepared",
          "Mixed-slice rigid edges have valid coupled tracks");
    g.sample(1);
  }
  check(close(initial, g.transforms()), "Mixed-slice coupled motion restores through its inverse");
  const auto record = move(custom, "U").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(record.dump())).at("status") == "Prepared",
        "Custom plain edge follows legal layer motion");
  g.sample(1);
  g.set_state_json(custom.state_json());
  const auto inverse = move(custom, "U'").at("transitions").at(0);
  check(Json::parse(g.prepare_animation_json(inverse.dump())).at("status") == "Prepared",
        "Custom plain-edge inverse prepares");
  g.sample(1);
  check(close(initial, g.transforms()), "Custom rigid edges restore through inverse motion");
}
} // namespace
int main() {
  try {
    models();
    custom_coupling();
    invalid_count_guards();
    animation("gear", "R U F (R U F)'");
    animation("gear-extreme", "Uq R Fq (Uq R Fq)'");
    animation("gear", "R U F (R U F)' R12", "-six-teeth");
    animation("gear-extreme", "Uq R Fq (Uq R Fq)'", "-six-teeth");
    animation("gear", "R U F (R U F)' R12", "-normal", false);
    animation("gear-extreme", "Uq R Fq (Uq R Fq)'", "-supplied", false);
    animation("gear-plain", "Rq Uq Fq (Rq Uq Fq)'", "-supplied", false);
    invalid_geometry();
    coupled_midpoint();
    coupled_midpoint(true);
    coupled_midpoint(true, true);
    fixed_pivot();
    supplied_model();
    supplied_plain_edges();
    std::cout << "Gear rules and articulated motions verified\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
