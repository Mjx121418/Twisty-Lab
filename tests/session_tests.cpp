#include "twisty/kernel.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
namespace {
std::size_t checks = 0;
void check(bool pass, const std::string &message) {
  ++checks;
  if (!pass)
    throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + path);
  std::ostringstream result;
  result << file.rdbuf();
  return result.str();
}
Json bundle(bool bandaged) {
  const auto module = language::cube_module(bandaged);
  return {{"kind", "relational-package"},
          {"formatVersion", 1},
          {"source", module.source},
          {"catalog", module.catalog}};
}
void invalid_unchanged(Session &session, const Json &document) {
  const auto before = session.snapshot();
  check(Json::parse(session.load_json(document.dump())).at("status") == "Invalid", "Invalid replay accepted");
  check(session.snapshot() == before, "Failed replay mutated session");
}
void elongated_bandage(Session &session, Geometry &geometry) {
  const auto scene = Json::parse(geometry.scene_json());
  std::map<std::string, Json> assets, ports;
  std::size_t body_count = 0, body_index = 0, index = 0;
  for (const auto &asset : scene.at("meshAssets"))
    assets[asset.at("id").get<std::string>()] = asset;
  for (const auto &part : scene.at("visualParts")) {
    if (part.at("pieceId") == "bandage/UF-UFR") {
      if (part.at("role") == "body") {
        ++body_count;
        body_index = index;
      } else
        ports[part.at("portId").get<std::string>()] = {{"index", index}, {"asset", part.at("meshAssetId")}};
    }
    ++index;
  }
  check(body_count == 1 && ports.size() == 5, "Elongated piece must have one body and five picking ports");
  check(assets.at("bandage/body").at("vertexCount") == 24 && assets.at("bandage/body").at("indexCount") == 36,
        "Elongated cuboid exceeded its shared mesh budget");
  const auto definition = session.definition();
  const auto catalog = language::cube_module(true).catalog;
  const auto labels = catalog.at("faceLabels").get<std::vector<std::string>>();
  const auto world = [&](const std::string &port, std::size_t vertex) {
    std::array<double, 3> point{};
    const auto offset =
        assets.at(ports.at(port).at("asset").get<std::string>()).at("positionOffset").get<std::size_t>();
    const auto matrix = ports.at(port).at("index").get<std::size_t>() * 16;
    for (int row = 0; row < 3; ++row) {
      point[row] = geometry.transforms()[matrix + 12 + row];
      for (int column = 0; column < 3; ++column)
        point[row] += geometry.transforms()[matrix + column * 4 + row] *
                      geometry.positions()[offset + vertex * 3 + column];
    }
    return point;
  };
  for (const auto &rotation : catalog.at("symmetry")) {
    auto state = definition->initial;
    for (Index p = 0; p < definition->pieces.size(); ++p) {
      const auto &piece = definition->pieces[p];
      const auto &domain = definition->domains[piece.domain];
      auto key = domain.id + ":";
      for (const auto &[port, attachment] : domain.placements[piece.home].ports) {
        (void)port;
        if (!key.ends_with(':'))
          key += '-';
        const auto label = std::find(labels.begin(), labels.end(), attachment.second) - labels.begin();
        key += labels.at(rotation.at(label).get<Index>());
      }
      state.placement_of[p] = domain.by_key.at(key);
    }
    geometry.set_state_json(encode_state(*definition, state).dump());
    for (const auto &pair : {std::pair{"0", "2"}, std::pair{"1", "4"}}) {
      int shared = 0;
      for (std::size_t a = 0; a < 4; ++a)
        for (std::size_t b = 0; b < 4; ++b) {
          const auto x = world(pair.first, a), y = world(pair.second, b);
          double distance = 0;
          for (int axis = 0; axis < 3; ++axis)
            distance += (x[axis] - y[axis]) * (x[axis] - y[axis]);
          if (distance < 1e-12)
            ++shared;
        }
      check(shared == 2, "Joined stickers have a gap or overlap in a compound placement");
    }
    const auto offset = body_index * 16;
    for (int a = 0; a < 3; ++a)
      for (int b = 0; b < 3; ++b) {
        double dot = 0;
        for (int row = 0; row < 3; ++row)
          dot += geometry.transforms()[offset + a * 4 + row] * geometry.transforms()[offset + b * 4 + row];
        check(std::abs(dot - (a == b ? 1 : 0)) < 1e-6, "Elongated placement frame is not rigid");
      }
  }
  geometry.set_state_json(session.state_json());
}
void model(bool bandaged) {
  const std::string name = bandaged ? "bandaged" : "cube3";
  const auto source = bundle(bandaged).dump();
  Session session(source), previous(source);
  const auto historical = Json::parse(read("packages/" + name + "/definition.json"));
  const auto initial = session.snapshot();
  check(initial.at("state") == previous.snapshot().at("state"), "Initial identities changed");
  check(Json(session.definition()->digest) != historical.at("definitionDigest"), "New semantics reused old pin");
  check(Json::parse(session.definition_json()).at("kind") == "relational-definition",
        "Definition format missing");
  Session restored(session.definition_json());
  check(session.definition_json() == session.execution()->definition_json().dump(),
        "Memory-bounded definition serialization changed canonical bytes");
  check(restored.snapshot() == initial, "Compiled definition round trip differs");
  auto damaged_definition = Json::parse(session.definition_json());
  damaged_definition["program"]["goalRoot"] = 0;
  check(compile_execution_json(damaged_definition.dump()).at("status") == "Invalid",
        "Tampered compiled program accepted");
  for (const auto *field : {"source", "catalog"}) {
    auto changed = Json::parse(session.definition_json());
    if (std::string(field) == "source")
      changed["source"]["goal"] = false;
    else
      changed["catalog"]["unrecognizedMetadata"] = true;
    check(compile_execution_json(changed.dump()).at("status") == "Invalid",
          "Moved source/catalog bypassed the persisted digest check");
  }
  auto state = initial.at("state");
  state["placementOf"]["unknown"] = "center:U";
  check(Json::parse(session.validate_state_json(state.dump())).at("status") == "Invalid",
        "Unknown state piece accepted");
  check(session.execute("U", "1").at("status") == "StaleRevision", "Stale edit accepted");
  auto preview = Json::parse(session.plan_json(R"({"operation":"U","parameters":{}})", session.state_json()));
  check(preview.at("status") == "Legal" && session.snapshot() == initial, "Pure preview mutated session");
  const auto witness = preview.at("transition");
  check(witness.at("request").at("choices").size() == (bandaged ? 4 : 3), "Missing completion");
  check(witness.at("pieceActions").size() == (bandaged ? 8 : 9), "Center participation changed");
  check(std::any_of(witness.at("pieceActions").begin(), witness.at("pieceActions").end(),
                    [](const auto &a) {
                      return a.at("pieceId") == "center/U" && a.at("from") == a.at("to") &&
                             a.at("event") == "U.clockwise";
                    }),
        "Self-loop event lost");
  for (const auto *kind : {"cube-euclidean", "cube-port-diagram", "cube-spherical"}) {
    auto realization = Json::parse(read("packages/" + name + "/" + kind + ".json"));
    bool mismatch = false;
    try {
      Geometry incompatible(session.execution(), realization.dump());
    } catch (const DiagnosticError &) {
      mismatch = true;
    }
    check(mismatch, "Old realization pin was silently rebound");
    realization["compatibleDefinitionDigest"] = session.definition()->digest;
    Geometry geometry(session.execution(), realization.dump());
    if (bandaged && std::string(kind) == "cube-euclidean")
      elongated_bandage(session, geometry);
    Geometry independent(session.definition_json(), realization.dump());
    check(geometry.positions() == independent.positions(), "Independent interpreter assets differ");
    check(Json::parse(geometry.prepare_animation_json(witness.dump())).at("status") == "Prepared",
          "Flat witness not realized");
    geometry.sample(.5);
    for (const auto x : geometry.transforms())
      check(std::isfinite(x), "Nonfinite animation frame");
    geometry.sample(1);
    const auto final_frame = geometry.transforms();
    independent.set_state_json(witness.at("afterState").dump());
    check(final_frame == independent.transforms(), "Animation endpoint differs from flat target");
    for (int corruption = 0; corruption < 4; ++corruption) {
      auto invalid = witness;
      if (corruption == 0)
        invalid["request"].erase("choices");
      if (corruption == 1)
        invalid["pieceActions"][0]["event"] = "U.counterclockwise";
      if (corruption == 2)
        invalid["pieceActions"].erase(invalid["pieceActions"].begin());
      if (corruption == 3)
        invalid["request"]["choices"][0] = "idle";
      check(Json::parse(geometry.prepare_animation_json(invalid.dump())).at("status") ==
                "UnsupportedTransition",
            "Tampered witness animated");
      check(final_frame == geometry.transforms(), "Rejected witness changed frame");
    }
  }
  // All requests at each sampled walk position, then compare the next state.
  std::uint32_t random = 19;
  for (int step = 0; step < 36; ++step) {
    for (const auto &op : session.definition()->operations) {
      const auto a = session.execution()->plan(session.state(), op.id);
      const auto b = previous.execution()->plan(previous.state(), op.id);
      check(a.transition.has_value() == b.transition.has_value(), "Session permission differs from oracle");
      if (a.transition)
        check(encode_state(*session.definition(), a.transition->after) ==
                  encode_state(*previous.definition(), b.transition->after),
              "Session target differs from oracle");
    }
    const auto legal = session.snapshot().at("legalRequests");
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    const auto move = legal.at(random % legal.size()).at("operation").get<std::string>();
    check(session.execute(move, session.revision()).at("status") == "Committed", "Legal flat move failed");
    previous.execute(move, previous.revision());
    check(session.snapshot().at("state") == previous.snapshot().at("state"), "Walk state differs");
    check(session.snapshot().at("solved") == previous.snapshot().at("solved"), "Goal differs");
  }
  const auto saved = session.save();
  check(saved.at("schemaVersion") == 2 && saved.at("logical").at("requests").at(0).contains("choices"),
        "History format lost choices");
  const auto before_undo = session.snapshot().at("stateDigest");
  check(session.undo(session.revision()).at("status") == "Committed", "Undo failed");
  check(session.redo(session.revision()).at("status") == "Committed" &&
            session.snapshot().at("stateDigest") == before_undo,
        "Redo differs");
  restored.load(saved);
  check(restored.snapshot().at("stateDigest") == before_undo, "Replay differs");
  for (int corruption = 0; corruption < 5; ++corruption) {
    auto invalid = saved;
    if (corruption == 0)
      invalid["logical"]["requests"][0].erase("choices");
    if (corruption == 1)
      invalid["logical"]["requests"][0]["choices"][0] = "idle";
    if (corruption == 2)
      invalid["logical"]["checkpoints"][0]["stateDigest"] = "bad";
    if (corruption == 3)
      invalid["schemaVersion"] = 1;
    if (corruption == 4)
      invalid["logical"]["requests"][0]["choices"] = nullptr;
    invalid_unchanged(restored, invalid);
  }
  auto old_save = session.save();
  old_save["schemaVersion"] = 1;
  invalid_unchanged(restored, old_save);
  Session a(source), b(source);
  check(a.scramble(42, 25, "0").at("scramble") == b.scramble(42, 25, "0").at("scramble"),
        "Scramble not reproducible");
  check(a.state_json() == b.state_json(), "Scramble states differ");
  if (bandaged) {
    Session blocked(source);
    check(blocked.run("U R", "transactional", "0").at("status") == "Committed",
          "Valid bandage sequence failed");
    Session transaction(source);
    check(transaction.run("D R", "transactional", "0").at("status") == "Blocked" &&
              transaction.revision() == "0",
          "Blocked transaction partially committed");
  }
}
void alternative() {
  auto package = bundle(false);
  auto terms = Json::array();
  for (const auto &d : package.at("source").at("domains"))
    terms.push_back({{"chosen", {{"domain", d.at("id")}, {"moves", {"idle"}}}}});
  for (auto &r : package["source"]["requests"])
    if (r.at("id") == "U" || r.at("id") == "U'")
      r["relation"] = {{"any", Json::array({r.at("relation"), {{"all", terms}}})}};
  Session session(package.dump());
  const auto origin = session.state_json();
  const auto idle = R"({"operation":"U","parameters":{},"choices":["idle","idle","idle"]})";
  check(Json::parse(session.execute_json(idle, "0")).at("status") == "Committed",
        "Explicit alternative completion failed");
  check(session.state_json() == origin, "Idle completion moved pieces");
  session.undo(session.revision());
  session.redo(session.revision());
  check(session.state_json() == origin, "Redo silently selected the default tuple");
  Session replay(package.dump());
  replay.load(session.save());
  check(replay.state_json() == origin && replay.snapshot().at("history").size() == 1,
        "Replay lost distinct completion");
  auto blocked_package = bundle(false);
  blocked_package["source"]["relations"].push_back({{"id", "test/block-all"}, {"predicate", false}});
  Session blocked(blocked_package.dump());
  check(blocked.execute("U", "0").at("status") == "Blocked" && blocked.revision() == "0",
        "Relational session ignored authored interaction");
  auto goal_package = bundle(false);
  goal_package["source"]["goal"] = false;
  Session unsolved(goal_package.dump());
  check(!unsolved.snapshot().at("solved").get<bool>(), "Relational session used the old home goal");
}
void retired_formats() {
  check(kernel_info().at("apiVersion") == 4 && kernel_info().at("sessionSchemaVersions") == Json::array({2}),
        "Runtime still advertises retired APIs");
  for (const auto &path : {"packages/cube3/source.json", "packages/cube3/definition.json",
                           "packages/helicopter/definition.json", "packages/gear/source.json"}) {
    const auto result = compile_execution_json(read(path));
    check(result.at("status") == "Invalid" &&
              result.at("diagnostics")[0].at("reasonCode") == "definition.unsupported",
          "Retired source silently entered a compatibility executor");
  }
}
void shared_definition() {
  const auto deep = std::string(520, '[') + "0" + std::string(520, ']');
  check(compile_execution_json(deep).at("diagnostics")[0].at("reasonCode") == "document.depth",
        "Execution document nesting limit was lost");
  std::shared_ptr<const Execution> context;
  {
    Session original(bundle(false).dump());
    context = original.execution();
    check(original.execute("U", "0").at("status") == "Committed", "Shared definition setup failed");
    Session fresh(context);
    check(fresh.execution() == context && fresh.revision() == "0" &&
              fresh.state() == context->definition()->initial,
          "Shared session copied mutable state or history");
    check(fresh.execute("F", "0").at("status") == "Committed" && original.revision() == "1" &&
              fresh.state() != original.state(),
          "Shared sessions did not keep independent histories");
  }
  Session surviving(context);
  check(surviving.execute("R", "0").at("status") == "Committed",
        "Definition did not survive its original session");
  bool rejected = false;
  try {
    Session invalid(std::shared_ptr<const Execution>{});
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Null shared definition accepted");
}
} // namespace
int main() {
  try {
    model(false);
    model(true);
    alternative();
    shared_definition();
    retired_formats();
    std::cout << checks << " relational session/interpreter checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
