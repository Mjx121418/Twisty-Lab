#include "twisty/kernel.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace twisty;
namespace language = twisty::language;
std::size_t checks = 0;
void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
Json read(const std::string &path) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + path);
  if (!file)
    throw std::runtime_error(path);
  return Json::parse(file);
}
const std::vector<std::string> edges{"UF", "UR", "UB", "UL", "DF", "DR", "DB", "DL", "FR", "FL", "BR", "BL"};
struct Comparison {
  std::string id;
  std::vector<std::string> geared;
  language::CubeModule module;
  language::CompiledPuzzle compiled;
  std::shared_ptr<const Definition> old;
  std::vector<Index> old_piece;
  std::vector<std::vector<Index>> global;
  std::vector<core::Choices> tuples;
  Json queries = Json::array(), expected = Json::array();
  Comparison(std::vector<std::string> mask, std::string name)
      : id(std::move(name)), geared(std::move(mask)), module(language::gear_module(geared, id)),
        compiled(language::compile(module.source)),
        old(load_definition(compile_gear_source({{"kind", "gear-cube-prototypes"},
                                                 {"schemaVersion", 1},
                                                 {"semanticsVersion", "2.0.2"},
                                                 {"puzzleId", id},
                                                 {"gearedEdges", geared}}))) {
    check(compiled.program.data().piece_count == 26 && compiled.program.data().placement_count == 198 &&
              compiled.program.data().requests.size() == 24,
          "Gear catalog sizes changed");
    for (const auto &p : compiled.bindings.pieces) {
      const auto found =
          std::find_if(old->pieces.begin(), old->pieces.end(), [&](const auto &x) { return x.id == p; });
      check(found != old->pieces.end(), "Piece disappeared");
      old_piece.push_back(static_cast<Index>(found - old->pieces.begin()));
    }
    for (const auto &d : old->domains) {
      std::vector<Index> map;
      for (const auto &q : d.placements) {
        const auto found = std::find(compiled.bindings.placements.begin(), compiled.bindings.placements.end(),
                                     d.id + "/" + q.key);
        check(found != compiled.bindings.placements.end(), "Placement disappeared");
        map.push_back(static_cast<Index>(found - compiled.bindings.placements.begin()));
        const auto &entry = module.catalog.at("placements").at(d.id + "/" + q.key);
        check(entry.at("resources").get<std::vector<std::string>>() == q.footprint, "Slot incidence changed");
        for (const auto &[port, value] : q.ports)
          check(entry.at("ports").at(port).at("cell").get<std::string>() == value.first &&
                    entry.at("ports").at(port).at("attachment").get<std::string>() == value.second,
                "Port attachment changed");
      }
      global.push_back(std::move(map));
    }
    for (const auto &r : compiled.bindings.requests) {
      const auto &profile = r.profiles.at(*r.default_profile);
      tuples.push_back(profile.choices);
    }
    check(flatten(old->initial) == compiled.program.data().initial, "Initial state changed");
  }
  core::State flatten(const State &state) const {
    core::State result;
    for (auto p : old_piece)
      result.push_back(global[old->pieces[p].domain][state.placement_of[p]]);
    return result;
  }
  std::string wrapper() const {
    return Json{{"kind", "relational-gear-prototypes"},
                {"formatVersion", 1},
                {"puzzleId", id},
                {"gearedEdges", geared}}
        .dump();
  }
  void tables() const {
    for (const auto &op : old->operations)
      for (Index d = 0; d < old->domains.size(); ++d) {
        const auto &domain = old->domains[d];
        const auto &moves = module.source.at("domains").at(d).at("moves");
        const auto found = std::find_if(moves.begin(), moves.end(), [&](const auto &m) {
          return m.at("id").template get<std::string>() == op.id;
        });
        check(found != moves.end(), "Local move missing");
        const auto unavailable =
            module.catalog.at("requests").at(op.id).at("unavailablePlacements").get<std::set<std::string>>();
        for (Index q = 0; q < domain.placements.size(); ++q) {
          const auto &key = domain.placements[q].key;
          check(found->at("transport").contains(key) == (op.roles[d][q] == 1),
                "Participation changed: " + op.id);
          check(unavailable.contains(domain.id + "/" + key) == (op.roles[d][q] == 2),
                "Unavailable frame row changed");
          if (op.roles[d][q] == 1)
            check(found->at("transport").at(key).template get<std::string>() ==
                      domain.placements[op.maps[d][q]].key,
                  "Transport/phase changed");
        }
      }
  }
  Json names(const core::Choices &choices) const {
    auto result = Json::array();
    for (Index d = 0; d < choices.size(); ++d)
      result.push_back(compiled.bindings.choice_values[d][choices[d]]);
    return result;
  }
  void compare(const State &state, Index r, bool emit) {
    const auto &id = compiled.bindings.requests[r].id;
    const auto previous = twisty::plan(*old, state, id);
    const auto planned = compiled.program.plan(r, flatten(state), tuples[r]);
    check(planned.transition.has_value() == previous.transition.has_value(),
          "Coupling permission differs: " + id);
    Json wanted{{"status", "Rejected"}, {"reason", "relation"}};
    if (planned.transition) {
      const auto &t = *planned.transition;
      check(t.after == flatten(previous.transition->after), "Target differs: " + id);
      auto actions = Json::array();
      for (const auto &a : previous.transition->actions) {
        const auto &p = old->pieces[a.piece];
        const auto pi = std::find(compiled.bindings.pieces.begin(), compiled.bindings.pieces.end(), p.id) -
                        compiled.bindings.pieces.begin();
        const auto event = std::find(compiled.bindings.events.begin(), compiled.bindings.events.end(),
                                     p.type + "/" + id + ".motion") -
                           compiled.bindings.events.begin();
        actions.push_back({{"piece", pi},
                           {"from", global[p.domain][a.from]},
                           {"to", global[p.domain][a.to]},
                           {"event", event}});
      }
      std::sort(actions.begin(), actions.end(),
                [](const auto &a, const auto &b) { return a.at("piece") < b.at("piece"); });
      auto actual = Json::array();
      for (const auto &a : t.actions)
        actual.push_back({{"piece", a.piece}, {"from", a.from}, {"to", a.to}, {"event", a.event}});
      check(actual == actions, "Exact participant/event witness differs");
      wanted = {{"status", "Permitted"},
                {"transition",
                 {{"request", id},
                  {"choices", names(tuples[r])},
                  {"before", flatten(state)},
                  {"after", flatten(previous.transition->after)},
                  {"actions", actions}}}};
      const auto inverse = old->operations[old->operation_ids.at(id)].inverse;
      const auto found = std::find_if(compiled.bindings.requests.begin(), compiled.bindings.requests.end(),
                                      [&](const auto &x) { return x.id == inverse; });
      const auto back = static_cast<Index>(found - compiled.bindings.requests.begin());
      const auto reversed = compiled.program.plan(back, t.after, tuples[back]);
      check(reversed.transition && reversed.transition->after == t.before, "Inverse tuple failed");
    } else
      check(planned.rejection == core::Rejection::relation, "Coupling reached the wrong rejection stage");
    if (emit) {
      queries.push_back(
          {{"op", "plan"}, {"request", id}, {"state", flatten(state)}, {"choices", names(tuples[r])}});
      expected.push_back(wanted);
    }
  }
  void states(bool emit) {
    for (Index p = 0; p < old->pieces.size(); ++p)
      for (Index q = 0; q < old->domains[old->pieces[p].domain].placements.size(); ++q) {
        auto state = old->initial;
        state.placement_of[p] = q;
        const bool valid = validate_state(*old, state).empty();
        check(compiled.program.valid(flatten(state)) == valid, "Imported assignment validity differs");
        if (valid)
          check(compiled.program.solved(flatten(state)) == solved(*old, state), "Goal differs");
      }
    auto state = old->initial;
    for (int step = 0; step < 16; ++step) {
      check(compiled.program.valid(flatten(state)), "Walk invariant failed");
      for (Index r = 0; r < tuples.size(); ++r)
        compare(state, r, emit && step < 3);
      const auto legal = legal_operations(*old, state);
      state =
          twisty::plan(*old, state,
                       legal[(step * 37 + geared.size()) % legal.size()].at("operation").get<std::string>())
              .transition->after;
    }
    auto wrong = tuples[0];
    wrong[3] = 0;
    check(!compiled.program.plan(0, compiled.program.data().initial, wrong).transition,
          "Incomplete coupling accepted");
  }
  void sessions_and_geometry() {
    Session session(wrapper());
    check(session.execution()->relational(), "Gear factory fell back to legacy execution");
    Session restored(session.definition_json());
    Session previous(old->canonical.dump());
    std::string algorithm;
    for (int i = 0; i < 5; ++i) {
      const auto legal = session.snapshot().at("legalRequests");
      const auto name = legal[(i * 7 + geared.size()) % legal.size()].at("operation").get<std::string>();
      algorithm += (i ? " " : "") + name;
      check(session.execute(name, session.revision()).at("status") == "Committed",
            "Migrated gear move failed");
      check(previous.execute(name, previous.revision()).at("status") == "Committed",
            "Legacy gear move failed");
      check(session.snapshot().at("state") == previous.snapshot().at("state"), "Wire state differs");
    }
    check(restored.load(session.save()).at("status") == "Loaded", "Gear choices failed replay");
    const auto final = restored.snapshot().at("stateDigest");
    check(restored.undo(restored.revision()).at("status") == "Committed" &&
              restored.redo(restored.revision()).at("status") == "Committed" &&
              restored.snapshot().at("stateDigest") == final,
          "Gear undo/redo failed");
    previous.undo(previous.revision());
    const auto converted =
        Json::parse(restored.convert_legacy_json(previous.save_json(), previous.definition_json()));
    check(converted.at("status") == "Converted", "Legacy gear conversion failed: " + converted.dump());
    check(restored.load(converted.at("session")).at("status") == "Loaded" &&
              restored.redo(restored.revision()).at("status") == "Committed" &&
              restored.snapshot().at("stateDigest") == final,
          "Converted gear redo failed");
    for (const auto &variant :
         {"gear-normal-euclidean", "gear-supplied-euclidean", "gear-euclidean", "gear-six-teeth-euclidean",
          "gear-port-diagram", "gear-six-teeth-port-diagram", "gear-spherical"}) {
      auto realization = read("packages/gear/" + std::string(variant) + ".json");
      realization["compatibleDefinitionDigest"] = previous.definition()->digest;
      Geometry old_view(previous.execution(), realization.dump());
      realization["compatibleDefinitionDigest"] = session.definition()->digest;
      Geometry view(session.execution(), realization.dump());
      check(view.positions() == old_view.positions() && view.normals() == old_view.normals() &&
                view.indices() == old_view.indices(),
            "Gear meshes changed");
      Session current(wrapper()), reference(old->canonical.dump());
      for (int i = 0; i < 3; ++i) {
        const auto legal = current.snapshot().at("legalRequests");
        const auto name = legal[(i * 5 + geared.size()) % legal.size()].at("operation").get<std::string>();
        const auto a = current.execute(name, current.revision()).at("transitions").at(0);
        const auto b = reference.execute(name, reference.revision()).at("transitions").at(0);
        check(Json::parse(view.prepare_animation_json(a.dump())).at("status") == "Prepared",
              "Domain event did not bind to a gear track");
        check(Json::parse(old_view.prepare_animation_json(b.dump())).at("status") == "Prepared",
              "Legacy track failed");
        for (double t : {0., .125, .5, .875, 1.}) {
          view.sample(t);
          old_view.sample(t);
          check(view.transforms() == old_view.transforms(), "Gear winding or world/local motion changed");
        }
        auto bad = a;
        bad["pieceActions"][0]["event"] = "wrong";
        const auto frame = view.transforms();
        check(Json::parse(view.prepare_animation_json(bad.dump())).at("status") == "UnsupportedTransition" &&
                  view.transforms() == frame,
              "Bad gear event changed the frame");
      }
    }
  }
};
void different_local_events() {
  Comparison fixture(edges, "gear-event-dispatch");
  auto source = fixture.module.source;
  // Permit an explicit alternative tuple while retaining the default tuple
  // and the request catalog used to certify all local movement tracks.
  for (auto &request : source["requests"]) {
    const auto id = request.at("id").get<std::string>();
    if (id == "F" || id == "F'")
      request["relation"]["all"][1] = {
          {"chosen", {{"domain", "corner"}, {"moves", Json::array({id, id == "F" ? "R" : "R'"})}}}};
  }
  Session current(Json{{"kind", "relational-package"},
                       {"formatVersion", 1},
                       {"source", source},
                       {"catalog", fixture.module.catalog}}
                      .dump());
  auto realization = read("packages/gear/gear-normal-euclidean.json");
  realization["compatibleDefinitionDigest"] = current.definition()->digest;
  Geometry view(current.execution(), realization.dump());
  realization["compatibleDefinitionDigest"] = fixture.old->digest;
  Geometry f_view(Execution::legacy(fixture.old), realization.dump());
  Geometry r_view(Execution::legacy(fixture.old), realization.dump());
  const auto origin = current.snapshot();
  const auto result = Json::parse(
      current.execute_json(R"({"operation":"F","parameters":{},"choices":["F","R","F","F"]})", "0"));
  check(result.at("status") == "Committed", "Alternative completed tuple was replaced by the default");
  const auto witness = result.at("transitions").at(0);
  check(Json::parse(view.prepare_animation_json(witness.dump())).at("status") == "Prepared",
        "Mixed local events failed to prepare");
  for (auto pair : {std::pair{&f_view, "F"}, std::pair{&r_view, "R"}}) {
    const auto planned = twisty::plan(*fixture.old, fixture.old->initial, pair.second);
    check(Json::parse(
              pair.first->prepare_animation_json(encode_transition(*fixture.old, *planned.transition).dump()))
                  .at("status") == "Prepared",
          "Mixed event oracle failed");
  }
  const auto parts = Json::parse(view.scene_json()).at("visualParts");
  for (double t : {0., .125, .5, .875, 1.}) {
    view.sample(t);
    f_view.sample(t);
    r_view.sample(t);
    for (std::size_t p = 0; p < parts.size(); ++p) {
      const auto id = parts.at(p).at("pieceId").get<std::string>();
      const auto &expected = id.starts_with("corner/") ? r_view.transforms() : f_view.transforms();
      for (int k = 0; k < 16; ++k)
        check(view.transforms()[p * 16 + k] == expected[p * 16 + k],
              "Interpreter used the global request instead of a piece event");
    }
  }
  const auto saved = current.save();
  check(current.undo(current.revision()).at("status") == "Committed" &&
            current.snapshot().at("state") == origin.at("state"),
        "Mixed inverse choices failed");
  check(current.load(saved).at("status") == "Loaded", "Mixed event replay selected a different completion");
}
} // namespace
int main(int argc, char **argv) {
  try {
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::vector<std::string>> masks{
        edges, {}, {"UF", "UR", "UB", "UL", "DF", "DR", "DB", "DL"}, {"UF"}, {"UF", "UR"}};
    for (const auto &edge : edges)
      masks.push_back({edge});
    for (int k = 0; k < 12; ++k) {
      std::vector<std::string> mask;
      for (int i = 0; i < 12; ++i)
        if (((i * 17 + k * 13) % 19) < k + 2)
          mask.push_back(edges[i]);
      masks.push_back(mask);
    }
    different_local_events();
    Json suites = Json::array();
    for (std::size_t i = 0; i < masks.size(); ++i) {
      Comparison test(masks[i], "gear-migration-" + std::to_string(i));
      test.tables();
      test.states(i < 5);
      if (i < 5) {
        test.sessions_and_geometry();
        suites.push_back({{"source", test.module.source.dump()},
                          {"catalog", test.module.catalog},
                          {"wrapper", test.wrapper()},
                          {"preview", language::inspect(test.compiled)},
                          {"queries", test.queries},
                          {"expected", test.expected}});
      }
    }
    if (argc == 3 && std::string(argv[1]) == "--emit") {
      std::ofstream out(argv[2]);
      out << suites.dump();
    }
    std::cout << checks << " Gear relational checks passed over " << masks.size() << " masks in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " seconds\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
