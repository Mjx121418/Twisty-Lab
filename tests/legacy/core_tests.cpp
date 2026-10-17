#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace twisty;
namespace {
int checks = 0;
void check(bool result, const std::string &message) {
  ++checks;
  if (!result)
    throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + path);
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}
template <class F> void rejects(F action, const std::string &code) {
  try {
    action();
  } catch (const DiagnosticError &error) {
    check(error.code == code, "Unexpected diagnostic " + error.code + ", wanted " + code);
    return;
  }
  throw std::runtime_error("Expected diagnostic " + code);
}
Json realization(const Definition &d, const std::string &kind) {
  return {{"schemaVersion", 1},
          {"id", kind},
          {"kind", kind},
          {"compatibleDefinitionDigest", d.digest},
          {"requiredCapabilities", Json::array({"triangle-meshes", "rigid-transforms"})}};
}
void test_hashes() {
  check(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty vector");
  check(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc vector");
  check(sha256(std::string(1000000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "SHA-256 multiblock vector");
}
void test_compiler() {
  const auto source = Json::parse(read("packages/cube3/source.json"));
  auto compiled = compile_source(source);
  const auto d = load_definition(compiled);
  check(d->pieces.size() == 26 && d->operations.size() == 12, "Cube piece and primitive counts");
  const auto group = compiled.at("symmetry").at("members").get<std::vector<Permutation>>();
  check(group.size() == 24, "Cube group order");
  std::vector<Permutation> faces, edges, corners;
  for (const auto &g : group) {
    if (g[0] == 0)
      faces.push_back(g);
    if (std::set<Index>{g[0], g[2]} == std::set<Index>{0, 2})
      edges.push_back(g);
    if (std::set<Index>{g[0], g[1], g[2]} == std::set<Index>{0, 1, 2})
      corners.push_back(g);
  }
  check(enumerate_cosets(group, faces).size() == 6, "Face cosets");
  check(enumerate_cosets(group, edges).size() == 12, "Edge cosets");
  check(enumerate_cosets(group, corners).size() == 8, "Corner cosets");
  check(compiled["symmetry"]["positionOrbits"]["corner"]["positions"].size() == 8,
        "Author-supplied corner stabilizer compiles eight cosets");
  check(compiled["symmetry"]["positionOrbits"]["edge"]["positions"].size() == 12,
        "Author-supplied edge stabilizer compiles twelve cosets");
  auto shuffled = compiled;
  shuffled.erase("definitionDigest");
  shuffled["provenance"] = {{"unrelated", "annotation"}};
  std::reverse(shuffled["pieces"].begin(), shuffled["pieces"].end());
  std::reverse(shuffled["operations"].begin(), shuffled["operations"].end());
  check(load_definition(shuffled)->digest == d->digest, "Digest ignores provenance and enumeration order");
  check(compile_source(compiled).at("definitionDigest") == d->digest,
        "Explicit definition and prototypes agree");
  check(Json::parse(read("packages/cube3/definition.json")).at("definitionDigest") == d->digest,
        "Checked-in explicit reference stays equivalent to its prototypes");
  auto bad = source;
  bad["group"]["limit"] = 2;
  rejects([&] { compile_source(bad); }, "symmetry.limit");
  bad = source;
  bad["group"]["generators"][0][0] = 2;
  rejects([&] { compile_source(bad); }, "symmetry.permutation");
  bad = source;
  bad["group"]["subgroupGenerators"]["center"] = bad["group"]["subgroupGenerators"]["edge"];
  rejects([&] { compile_source(bad); }, "symmetry.stabilizer");
  auto invalid = compiled;
  invalid.erase("definitionDigest");
  invalid["initialState"]["mechanism"]["phase"] = 0.25;
  rejects([&] { load_definition(invalid); }, "value.inexact");
  auto assignment = encode_state(*d, d->initial);
  assignment["placementOf"]["corner/UFR"] = assignment["placementOf"]["corner/UBR"];
  rejects([&] { decode_state(*d, assignment); }, "state.occupancy");
}
void test_rules_and_sessions() {
  Session cube(read("packages/cube3/source.json"));
  const auto origin = cube.snapshot();
  const auto moved = cube.execute("U", "0");
  check(moved.at("status") == "Committed", "U committed");
  const auto after = moved.at("transitions").at(0).at("afterState").at("placementOf");
  check(after.at("edge/UF") == "edge:U-L", "U sends UF to UL with exact port orientation");
  bool center = false;
  for (const auto &action : moved.at("transitions").at(0).at("pieceActions"))
    if (action.at("pieceId") == "center/U" && action.at("from") == action.at("to"))
      center = true;
  check(center, "Unchanged center remains a participant");
  cube.undo(cube.revision());
  check(cube.snapshot().at("stateDigest") == origin.at("stateDigest"), "Undo restores exact state");
  check(cube.revision() == "2", "Undo revision remains monotonic");
  cube.redo(cube.revision());
  cube.undo(cube.revision());
  cube.execute("F", cube.revision());
  check(!cube.snapshot().at("canRedo").get<bool>(), "New continuation truncates redo");
  const auto before = cube.snapshot();
  check(cube.execute("U", "0").at("status") == "StaleRevision", "Stale command rejected");
  check(cube.snapshot() == before, "Stale command does not mutate session");
  check(parse_notation(*cube.definition(), "[R,U]") == std::vector<std::string>({"R", "U", "R'", "U'"}),
        "Commutator expansion");
  check(parse_notation(*cube.definition(), "(R U)' # comment\nF2") ==
            std::vector<std::string>({"U'", "R'", "F", "F"}),
        "Inverse groups, comments, and half turns");
  check(parse_notation(*cube.definition(), "[R:U]") == std::vector<std::string>({"R", "U", "R'"}),
        "Conjugate expansion");
  rejects([&] { parse_notation(*cube.definition(), "(R U"); }, "notation.invalid");
  Session bandage(read("packages/bandaged/source.json"));
  const auto blocked = bandage.execute("R", "0");
  check(blocked.at("reasonCode") == "footprint.partial_overlap", "Bandage blocks R");
  check(blocked.at("implicatedPieces").at(0) == "bandage/UF-UFR", "Blocking evidence identifies composite");
  check(bandage.revision() == "0", "Blocked command does not advance revision");
  check(bandage.run("U U' R", "transactional", "0").at("failureIndex") == 2,
        "Transactional batch reports failing index");
  check(bandage.revision() == "0" && bandage.snapshot().at("solved") == true,
        "Failed batch commits no prefix");
  check(bandage.run("U U' R", "interactive", "0").at("committed") == 2,
        "Interactive batch keeps legal prefix");
  check(bandage.revision() == "2", "Interactive prefix records commits");
  bandage.execute("U", bandage.revision());
  check(bandage.execute("R", bandage.revision()).at("status") == "Committed", "R enabled after U");
  auto mode = compile_source(Json::parse(read("packages/cube3/source.json")));
  mode.erase("definitionDigest");
  mode["initialState"]["mechanism"]["phase"] = "open";
  for (auto &operation : mode["operations"]) {
    if (operation.at("id") == "U") {
      operation["mechanismGuard"] = {{"phase", "open"}};
      operation["mechanismUpdate"] = {{"phase", "closed"}};
    }
    if (operation.at("id") == "U'") {
      operation["mechanismGuard"] = {{"phase", "closed"}};
      operation["mechanismUpdate"] = {{"phase", "open"}};
    }
  }
  Session mechanism(mode.dump());
  const auto mechanical_origin = mechanism.snapshot();
  check(mechanism.execute("U'", "0").at("reasonCode") == "mechanism.disabled",
        "Exact mechanism guard blocks inverse in wrong mode");
  mechanism.execute("U", "0");
  mechanism.undo(mechanism.revision());
  check(mechanism.snapshot().at("stateDigest") == mechanical_origin.at("stateDigest"),
        "Inverse restores hidden mechanism variable");
}
void test_indexed_occupancy() {
  for (const auto &name : {"cube3", "bandaged", "helicopter"}) {
    Session session(read("packages/" + std::string(name) + "/definition.json"));
    const auto d = session.definition();
    // Preserve the complete externally visible diagnostics against a reference
    // using resource names, including empty cells in the footprint backend.
    auto reference = [&](const State &state) {
      std::map<std::string, std::vector<std::string>> occupants;
      if (!d->placement_relations)
        for (const auto &cell : d->canonical.at("cells"))
          occupants[cell.get<std::string>()] = {};
      for (Index i = 0; i < d->pieces.size(); ++i) {
        const auto &piece = d->pieces[i];
        for (const auto &cell : d->domains[piece.domain].placements[state.placement_of[i]].footprint)
          occupants[cell].push_back(piece.id);
      }
      auto diagnostics = Json::array();
      for (const auto &[cell, pieces] : occupants)
        if (pieces.size() > 1 || (!d->placement_relations && pieces.empty()))
          diagnostics.push_back(
              {{"reasonCode", "state.occupancy"},
               {"source", cell},
               {"implicatedPieces", pieces},
               {"message", d->placement_relations ? "An exclusion cell can have at most one occupant."
                                                  : "Each declared cell must have exactly one occupant."}});
      return diagnostics;
    };
    check(validate_state(*d, d->initial) == reference(d->initial), "Initial indexed occupancy agrees");
    for (Index i = 0; i < d->pieces.size(); ++i)
      for (Index j = i + 1; j < d->pieces.size(); ++j)
        if (d->pieces[i].domain == d->pieces[j].domain) {
          auto invalid = d->initial;
          invalid.placement_of[i] = invalid.placement_of[j];
          check(validate_state(*d, invalid) == reference(invalid),
                "Indexed occupancy preserves conflict and vacancy diagnostics");
        }
    for (int step = 0; step < 12; ++step) {
      const auto before = session.snapshot();
      const auto requests = before.at("legalRequests");
      const auto move = requests.at((step * 37) % requests.size()).at("operation").get<std::string>();
      session.execute(move, session.revision());
      const auto after = session.snapshot();
      check(after.at("state") == encode_state(*d, session.state()) &&
                after.at("legalRequests") == legal_operations(*d, session.state()) &&
                after.at("revision") == session.revision(),
            "Snapshot cache follows state and legality at every revision");
      check(validate_state(*d, session.state()) == reference(session.state()), "Moved occupancy agrees");
      check(session.snapshot() == after, "Repeated snapshot is stable");
    }
  }
}
void test_scramble_and_replay() {
  auto renamed = compile_source(Json::parse(read("packages/cube3/source.json")));
  renamed.erase("definitionDigest");
  for (auto &operation : renamed["operations"]) {
    operation["id"] = operation.at("id").get<std::string>() + "/2";
    operation["inverse"] = operation.at("inverse").get<std::string>() + "/2";
  }
  Session named(renamed.dump());
  check(named.scramble(42, 8, "0").at("scramble").at("requests").size() == 8,
        "Scrambles execute operation IDs directly, including names outside notation syntax");
  Session replayed(renamed.dump());
  replayed.load(named.save());
  check(replayed.snapshot().at("stateDigest") == named.snapshot().at("stateDigest"),
        "Arbitrary operation IDs remain portable in saved request records");
  for (const auto &filename : {"packages/cube3/source.json", "packages/bandaged/source.json"}) {
    const auto source = read(filename);
    Session a(source), b(source);
    const auto origin = a.snapshot().at("stateDigest");
    const auto scramble = a.scramble(42, 110, "0"), same = b.scramble(42, 110, "0");
    check(scramble.at("scramble") == same.at("scramble") &&
              a.snapshot().at("stateDigest") == b.snapshot().at("stateDigest"),
          "Seeded walks are reproducible");
    a.undo(a.revision());
    Session loaded(source);
    const auto saved = a.save();
    loaded.load(saved);
    check(loaded.snapshot().at("stateDigest") == a.snapshot().at("stateDigest"),
          "Save/load preserves cursor state");
    check(loaded.snapshot().at("canRedo") == true, "Save/load preserves redo continuation");
    const auto before = loaded.snapshot();
    auto bad = saved;
    bad["logical"]["checkpoints"][0]["stateDigest"] = "bad";
    rejects([&] { loaded.load(bad); }, "history.checkpoint");
    check(loaded.snapshot() == before, "Invalid save leaves session unchanged");
    bad = saved;
    bad["logical"]["definitionDigest"] = "different";
    rejects([&] { loaded.load(bad); }, "definition.digest_mismatch");
    while (b.snapshot().at("canUndo").get<bool>())
      b.undo(b.revision());
    check(b.snapshot().at("stateDigest") == origin, "Scramble undo restores origin");
  }
}
void test_symmetry_covariance() {
  const std::vector<std::string> faces{"U", "R", "F", "D", "L", "B"};
  for (const auto &filename : {"packages/cube3/source.json", "packages/bandaged/source.json"}) {
    Session session(read(filename));
    const auto d = session.definition();
    const auto group = d->canonical.at("symmetry").at("members").get<std::vector<Permutation>>();
    const auto original = session.state();
    session.scramble(71, 10, session.revision());
    for (const auto &state : {original, session.state()}) {
      for (const auto &g : group) {
        auto acted = [&](const State &source) {
          auto result = source;
          for (Index i = 0; i < d->pieces.size(); ++i) {
            const auto &domain = d->domains[d->pieces[i].domain];
            const auto &placement = domain.placements[source.placement_of[i]];
            auto key = domain.id + ":";
            bool first = true;
            for (const auto &[_, port] : placement.ports) {
              if (!first)
                key += '-';
              first = false;
              const auto index = std::find(faces.begin(), faces.end(), port.second) - faces.begin();
              key += faces.at(g.at(static_cast<std::size_t>(index)));
            }
            result.placement_of[i] = domain.by_key.at(key);
          }
          return result;
        };
        const auto transformed = acted(state);
        check(validate_state(*d, transformed).empty(), "Symmetry preserves state occupancy");
        for (const auto &operation : d->operations) {
          const auto face = std::find(faces.begin(), faces.end(), operation.family) - faces.begin();
          const auto generated =
              faces.at(g.at(static_cast<std::size_t>(face))) + (operation.id.ends_with("'") ? "'" : "");
          const auto a = plan(*d, state, operation.id), b = plan(*d, transformed, generated);
          check(a.transition.has_value() == b.transition.has_value(), "Guards obey symmetry covariance");
          if (a.transition)
            check(acted(a.transition->after) == b.transition->after, "Transport obeys symmetry covariance");
        }
      }
    }
  }
}
void test_helicopter() {
  const auto text = read("packages/helicopter/definition.json");
  Session session(text);
  const auto d = session.definition();
  check(d->pieces.size() == 44 && d->operations.size() == 180, "Helicopter pieces and stop requests");
  check(legal_operations(*d, d->initial).size() == 60, "All five stops available on each solved grip");
  for (const auto &op : d->operations) {
    const auto forward = plan(*d, d->initial, op.id);
    if (!forward.transition)
      continue;
    const auto reverse = plan(*d, forward.transition->after, op.inverse);
    check(reverse.transition && reverse.transition->after == d->initial,
          "Every solved stop has an exact inverse");
  }
  const auto turn = plan(*d, d->initial, "UF_ab");
  check(turn.transition && turn.transition->actions.size() == 7,
        "Jumble moves two corners, four centers and a hidden edge");
  const auto wrong_phase = plan(*d, turn.transition->after, "UF_ac");
  check(!wrong_phase.transition && wrong_phase.blocked.at("reasonCode") == "placement.guard",
        "Wrong source stop is blocked");
  check(session.run("UF_ab UL_af", "transactional", "0").at("status") == "Committed",
        "Published two-step jumble is legal");
  const auto saved = session.save();
  session.undo(session.revision());
  session.undo(session.revision());
  check(session.state() == d->initial, "Undo unjumbles exactly");
  session.load(saved);
  check(session.save().at("logical").at("requests") == saved.at("logical").at("requests"),
        "Jumbled history replays exactly");

  // Ordinary half turns preserve four six-center orbits; the published jumble
  // sequence returns to cube shape while transporting centers between orbits.
  Index center_domain = 0;
  for (Index i = 0; i < d->domains.size(); ++i)
    if (d->domains[i].id == "center")
      center_domain = i;
  std::map<Index, int> orbit;
  int count = 0;
  for (const auto &piece : d->pieces) {
    if (piece.type != "center" || orbit.contains(piece.home))
      continue;
    std::vector<Index> frontier{piece.home};
    orbit[piece.home] = count;
    for (std::size_t i = 0; i < frontier.size(); ++i)
      for (const auto &op : d->operations)
        if (op.id.ends_with("_ad") && op.roles[center_domain][frontier[i]] == 1) {
          const auto q = op.maps[center_domain][frontier[i]];
          if (!orbit.contains(q)) {
            orbit[q] = count;
            frontier.push_back(q);
          }
        }
    check(frontier.size() == 6, "Each ordinary center orbit contains six placements");
    ++count;
  }
  check(count == 4 && orbit.size() == 24, "Four ordinary center orbits");
  Session exchanged(text);
  check(exchanged.run("UF_ab DR_ab FR_ad DR_ba UF_ba", "transactional", "0").at("status") == "Committed",
        "Published center-exchange jumble sequence is legal");
  int crossed = 0;
  for (Index i = 0; i < d->pieces.size(); ++i)
    if (d->pieces[i].type == "center") {
      check(orbit.contains(exchanged.state().placement_of[i]),
            "Center returns to an ordinary cube placement");
      crossed += orbit.at(exchanged.state().placement_of[i]) != orbit.at(d->pieces[i].home);
    }
  check(crossed == 2, "Jumble exchanges exactly two centers across ordinary orbits");

  State walked = d->initial;
  for (int i = 0; i < 80; ++i) {
    const auto requests = legal_operations(*d, walked);
    check(!requests.empty(), "Reachable jumble has a legal continuation");
    const auto &id = requests.at((i * 37) % requests.size()).at("operation");
    const auto forward = plan(*d, walked, id);
    const auto reverse = plan(*d, forward.transition->after, d->operations[d->operation_ids.at(id)].inverse);
    check(reverse.transition && reverse.transition->after == walked,
          "Jumbled inverse restores labels and hidden phases");
    walked = forward.transition->after;
  }
  auto invalid = d->canonical;
  auto assignment = encode_state(*d, d->initial);
  std::vector<std::string> corners;
  for (const auto &piece : d->pieces)
    if (piece.type == "corner")
      corners.push_back(piece.id);
  assignment["placementOf"][corners[1]] = assignment["placementOf"][corners[0]];
  rejects([&] { decode_state(*d, assignment); }, "state.occupancy");
  const auto &home_corner = *std::find_if(d->pieces.begin(), d->pieces.end(),
                                          [](const Piece &piece) { return piece.type == "corner"; });
  const auto &footprint = d->domains[home_corner.domain].placements[home_corner.home].footprint;
  bool conflict_checked = false;
  for (const auto &q : d->domains[center_domain].placements) {
    for (const auto &cell : q.footprint)
      if (cell.starts_with("exclusion/") &&
          std::find(footprint.begin(), footprint.end(), cell) != footprint.end()) {
        assignment = encode_state(*d, d->initial);
        const auto &center = *std::find_if(d->pieces.begin(), d->pieces.end(),
                                           [](const Piece &piece) { return piece.type == "center"; });
        assignment["placementOf"][center.id] = q.key;
        rejects([&] { decode_state(*d, assignment); }, "state.occupancy");
        conflict_checked = true;
        break;
      }
    if (conflict_checked)
      break;
  }
  check(conflict_checked, "A cross-domain exclusion rejects overlapping corner and center placements");
  auto &rule = invalid["operations"][0]["placementRules"]["corner"];
  const auto from = rule["transports"].begin().key();
  rule["blocked"].push_back(from);
  rejects([&] { load_definition(invalid); }, "relation.duplicate");
  invalid = d->canonical;
  invalid["operations"][0]["pieceGuards"]["mechanism/UF"] = "missing";
  rejects([&] { load_definition(invalid); }, "guard.placement");
  invalid = d->canonical;
  invalid["operations"][0]["selectedCells"] = Json::array();
  rejects([&] { load_definition(invalid); }, "rule.fields");
}
void test_helicopter_geometry() {
  Session session(read("packages/helicopter/definition.json"));
  const auto d = session.definition();
  for (const auto &kind : {"euclidean", "port-diagram"}) {
    const bool diagram = std::string(kind) == "port-diagram";
    const auto package = read("packages/helicopter/helicopter-" + std::string(kind) + ".json");
    Geometry geometry(session.definition_json(), package);
    const auto scene = Json::parse(geometry.scene_json());
    check(scene.at("catalogTransportVerified") == true, "All catalog transports registered geometrically");
    check(scene.at("visualParts").size() == (diagram ? 48 : 92), "Expected shared bodies and labeled ports");
    std::map<std::string, Json> assets;
    for (const auto &asset : scene.at("meshAssets")) {
      assets[asset.at("id")] = asset;
      const auto start = asset.at("indexOffset").get<std::size_t>();
      const auto end = start + asset.at("indexCount").get<std::size_t>();
      check(end <= geometry.indices().size(), "Mesh index slice is in bounds");
      for (std::size_t i = start; i < end; ++i)
        check(geometry.indices()[i] < asset.at("vertexCount"), "Shared mesh indices are local to the asset");
    }
    if (!diagram) {
      const std::map<std::string, std::array<double, 3>> normals{{"U", {0, 1, 0}}, {"D", {0, -1, 0}},
                                                                 {"F", {0, 0, 1}}, {"B", {0, 0, -1}},
                                                                 {"R", {1, 0, 0}}, {"L", {-1, 0, 0}}};
      const auto &transforms = geometry.transforms();
      for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i) {
        const auto &part = scene.at("visualParts")[i];
        if (part.at("role") != "port")
          continue;
        const auto offset = assets.at(part.at("meshAssetId")).at("positionOffset").get<std::size_t>();
        const auto &expected = normals.at(part.at("materialBindingId"));
        for (int row = 0; row < 3; ++row) {
          double actual = 0;
          for (int column = 0; column < 3; ++column)
            actual += transforms[i * 16 + column * 4 + row] * geometry.normals()[offset + column] / 1.4;
          check(std::abs(actual - expected[row]) < 1e-6, "Solved port color faces the correct direction");
        }
      }
    }
    auto verify = [&](const Transition &transition) {
      geometry.set_state_json(encode_state(*d, transition.before).dump());
      const auto initial = geometry.transforms();
      const auto prepared =
          Json::parse(geometry.prepare_animation_json(encode_transition(*d, transition).dump()));
      check(prepared.at("status") == "Prepared", "Legal Helicopter transition has a geometric route");
      check(geometry.transforms() == initial, "Catalog animation starts at the canonical source");
      geometry.sample(0.5);
      check(std::all_of(geometry.transforms().begin(), geometry.transforms().end(),
                        [](float value) { return std::isfinite(value); }),
            "Jumble intermediate frame is finite");
      geometry.sample(1 - 1e-7);
      const auto near_target = geometry.transforms();
      geometry.sample(1);
      const auto target = geometry.transforms();
      geometry.set_state_json(encode_state(*d, transition.after).dump());
      check(geometry.transforms() == target,
            "Catalog endpoint matches an independently rebuilt resting scene");
      if (!diagram) {
        // Compare actual mesh points before the canonical endpoint. This catches
        // discontinuities hidden by sample(1), including quotient edge poses.
        for (std::size_t p = 0; p < scene.at("visualParts").size(); ++p) {
          const auto &part = scene.at("visualParts")[p];
          const auto &asset = assets.at(part.at("meshAssetId"));
          const auto offset = asset.at("positionOffset").get<std::size_t>();
          const auto count = asset.at("vertexCount").get<std::size_t>();
          auto point = [&](const auto &matrices, std::size_t v) {
            std::array<double, 3> point{};
            for (int row = 0; row < 3; ++row)
              for (int column = 0; column < 3; ++column)
                point[row] +=
                    matrices[p * 16 + column * 4 + row] * geometry.positions()[offset + v * 3 + column];
            return point;
          };
          for (std::size_t v = 0; v < count; ++v) {
            const auto actual = point(near_target, v);
            bool found = false;
            for (std::size_t w = 0; w < count; ++w) {
              const auto expected = point(target, w);
              double distance = 0;
              for (int row = 0; row < 3; ++row)
                distance += (actual[row] - expected[row]) * (actual[row] - expected[row]);
              found = found || distance < 1e-12;
            }
            check(found, "Continuous track reaches the target mesh, allowing only declared asset symmetry");
          }
        }
      }
    };
    for (const auto &operation : d->operations) {
      const auto planned = plan(*d, d->initial, operation.id);
      if (planned.transition)
        verify(*planned.transition);
    }
    auto state = d->initial;
    for (int step = 0; step < 20; ++step) {
      const auto requests = legal_operations(*d, state);
      const auto id = requests[(step * 37) % requests.size()].at("operation").get<std::string>();
      const auto transition = *plan(*d, state, id).transition;
      verify(transition);
      state = transition.after;
    }
    for (const auto &part : scene.at("visualParts")) {
      const auto hit = Json::parse(geometry.bind_hit_json(part.at("visualPartId")));
      check(hit.at("pieceId") == part.at("pieceId"), "Catalog hit preserves the persistent piece identity");
      if (part.contains("portId"))
        check(hit.at("portId") == part.at("portId"), "Catalog hit preserves the port identity");
    }
    auto invalid = Json::parse(package);
    invalid["operationTracks"]["UF_ab"]["axis"] = {1, 0, 0};
    rejects([&] { Geometry bad(session.definition_json(), invalid.dump()); }, "realization.catalog");
    invalid = Json::parse(package);
    invalid["models"]["edge"]["symmetries"][1] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    rejects([&] { Geometry bad(session.definition_json(), invalid.dump()); }, "realization.catalog");
    check(session.state() == d->initial, "Catalog geometry never changes authoritative state");
  }
}
void test_geometry() {
  for (const auto &filename : {"packages/cube3/source.json", "packages/bandaged/source.json"}) {
    Session session(read(filename));
    const auto d = session.definition();
    for (const auto &kind : {"cube-euclidean", "cube-port-diagram"}) {
      Geometry geometry(session.definition_json(), realization(*d, kind).dump());
      for (const auto &operation : d->operations) {
        const auto planned = plan(*d, d->initial, operation.id);
        if (!planned.transition)
          continue;
        geometry.set_state_json(encode_state(*d, d->initial).dump());
        const auto before = geometry.transforms();
        check(Json::parse(geometry.prepare_animation_json(encode_transition(*d, *planned.transition).dump()))
                      .at("status") == "Prepared",
              "Geometric route agrees with exact port action");
        geometry.sample(0);
        check(geometry.transforms() == before, "Animation starts at source scene");
        geometry.sample(0.5);
        check(std::all_of(geometry.transforms().begin(), geometry.transforms().end(),
                          [](float value) { return std::isfinite(value); }),
              "Intermediate transform is finite");
        geometry.sample(1);
        const auto endpoint = geometry.transforms();
        geometry.set_state_json(encode_state(*d, planned.transition->after).dump());
        check(geometry.transforms() == endpoint, "Animation ends at independently rebuilt target scene");
      }
      const auto scene = Json::parse(geometry.scene_json());
      for (const auto &part : scene.at("visualParts")) {
        const auto target = Json::parse(geometry.bind_hit_json(part.at("visualPartId")));
        check(target.at("pieceId") == part.at("pieceId"), "Picking preserves persistent identity");
      }
      check(session.snapshot().at("stateDigest") == state_digest(*d, d->initial),
            "Geometry never mutates authoritative session");
    }
  }
}
} // namespace
int main() {
  try {
    test_hashes();
    test_compiler();
    test_rules_and_sessions();
    test_indexed_occupancy();
    test_scramble_and_replay();
    test_symmetry_covariance();
    test_helicopter();
    test_helicopter_geometry();
    test_geometry();
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "After " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
