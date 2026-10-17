#include "twisty/kernel.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using namespace twisty;
namespace language = twisty::relational;
std::size_t checks = 0;
std::string source_directory;
void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}
Json read(const std::string &name) {
  const auto directory = name.starts_with("relational") && !source_directory.empty()
                             ? source_directory
                             : std::string(PROJECT_ROOT) + "/packages/helicopter";
  std::ifstream file(directory + "/" + name);
  if (!file)
    throw std::runtime_error("Missing Helicopter fixture: " + name);
  return Json::parse(file);
}
template <class T> Index find(const std::vector<T> &values, const T &value) {
  const auto it = std::find(values.begin(), values.end(), value);
  if (it == values.end())
    throw std::runtime_error("Missing symbol: " + value);
  return static_cast<Index>(it - values.begin());
}
std::string group(const std::string &domain) {
  return domain.starts_with("edge/") ? "mechanism" : domain;
}
struct Comparison {
  Json source = read("relational.pzl.json"), catalog = read("relational-bindings.json");
  language::CompiledPuzzle compiled = language::compile(source);
  std::shared_ptr<const Definition> old = load_definition(read("definition.json"));
  std::vector<Index> old_piece;
  std::vector<std::vector<Index>> placements;
  std::vector<flat::Choices> tuples;
  Json queries = Json::array(), expected = Json::array();
  std::set<std::string> exercised;
  Comparison() {
    const auto &data = compiled.program.data();
    check(data.piece_count == 44 && data.placement_count == 420 && data.requests.size() == 180,
          "Catalog size changed");
    check(compiled.bindings.choice_slots == std::vector<std::string>({"center", "corner", "mechanism"}),
          "Expected three authoring domains");
    check(data.predicates.size() < 10000 && data.maps.size() < 300, "Compact lowering regressed");
    std::vector<std::string> old_ids;
    for (const auto &p : old->pieces)
      old_ids.push_back(p.id);
    for (const auto &id : compiled.bindings.pieces)
      old_piece.push_back(find(old_ids, id));
    for (const auto &d : old->domains) {
      std::vector<Index> map;
      for (const auto &q : d.placements)
        map.push_back(find(compiled.bindings.placements, group(d.id) + "/" + q.key));
      placements.push_back(std::move(map));
    }
    for (const auto &r : compiled.bindings.requests) {
      flat::Choices tuple;
      for (const auto &values : compiled.bindings.choice_values)
        tuple.push_back(find(values, r.id));
      tuples.push_back(std::move(tuple));
    }
    check(to_flat(old->initial) == data.initial, "Initial identity mapping changed");
  }
  flat::State to_flat(const State &state) const {
    flat::State result;
    for (const auto p : old_piece)
      result.push_back(placements[old->pieces[p].domain][state.placement_of[p]]);
    return result;
  }
  Json named(const flat::Choices &tuple) const {
    auto names = Json::array();
    for (Index i = 0; i < tuple.size(); ++i)
      names.push_back(compiled.bindings.choice_values[i][tuple[i]]);
    return names;
  }
  Json actions(const Transition &transition) const {
    auto result = Json::array();
    const auto &op = old->operations[old->operation_ids.at(transition.operation)];
    for (const auto &a : transition.actions) {
      const auto &p = old->pieces[a.piece];
      result.push_back({{"piece", find(compiled.bindings.pieces, p.id)},
                        {"from", placements[p.domain][a.from]},
                        {"to", placements[p.domain][a.to]},
                        {"event", find(compiled.bindings.events, op.transport)}});
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.at("piece") < b.at("piece"); });
    return result;
  }
  void tables() const {
    for (const auto &p : old->pieces) {
      const auto &c = catalog.at("pieces").at(p.id);
      check(c.at("portLabels").get<std::map<std::string, std::string>>() == p.labels, "Labels changed");
      check(c.at("home").get<std::string>() ==
                group(old->domains[p.domain].id) + "/" + old->domains[p.domain].placements[p.home].key,
            "Home changed");
    }
    for (const auto &d : old->domains)
      for (const auto &q : d.placements) {
        const auto &c = catalog.at("placements").at(group(d.id) + "/" + q.key);
        check(c.at("resources").get<std::vector<std::string>>() == q.footprint,
              "Exclusion incidence changed");
        for (const auto &[port, attachment] : q.ports)
          check(c.at("ports").at(port).at("cell").get<std::string>() == attachment.first &&
                    c.at("ports").at(port).at("attachment").get<std::string>() == attachment.second,
                "Port changed");
      }
    for (const auto &op : old->operations) {
      const auto &binding = catalog.at("requests").at(op.id);
      const auto unavailable = binding.at("unavailablePlacements").get<std::set<std::string>>();
      check(binding.at("inverse").get<std::string>() == op.inverse &&
                binding.at("event").get<std::string>() == op.transport,
            "Inverse/event changed");
      for (Index d = 0; d < old->domains.size(); ++d) {
        const auto &domain = old->domains[d];
        const auto &moves =
            source.at("domains").at(find(compiled.bindings.choice_slots, group(domain.id))).at("moves");
        const auto movement = std::find_if(moves.begin(), moves.end(), [&](const auto &m) {
          return m.at("id").template get<std::string>() == op.id;
        });
        check(movement != moves.end(), "Missing local movement");
        for (Index q = 0; q < domain.placements.size(); ++q) {
          const auto &key = domain.placements[q].key;
          const auto qualified = group(domain.id) + "/" + key;
          check(unavailable.contains(qualified) == (op.roles[d][q] == 2), "Unavailable section changed");
          check(movement->at("transport").contains(key) == (op.roles[d][q] == 1), "Participation changed");
          if (op.roles[d][q] == 1)
            check(movement->at("transport").at(key).template get<std::string>() ==
                      domain.placements[op.maps[d][q]].key,
                  "Exact transport changed");
        }
      }
    }
  }
  void compare(const State &state, Index request, bool export_case) {
    const auto &id = compiled.bindings.requests[request].id;
    const auto legacy = twisty::plan(*old, state, id);
    const auto planned = compiled.program.plan(request, to_flat(state), tuples[request]);
    check(planned.transition.has_value() == legacy.transition.has_value(), "Permission differs: " + id);
    Json answer;
    if (planned.transition) {
      exercised.insert(id);
      const auto &t = *planned.transition;
      check(t.after == to_flat(legacy.transition->after), "Target differs: " + id);
      Json actual = Json::array();
      for (const auto &a : t.actions)
        actual.push_back({{"piece", a.piece}, {"from", a.from}, {"to", a.to}, {"event", a.event}});
      const auto wanted = actions(*legacy.transition);
      check(actual == wanted, "Participation or winding event differs: " + id);
      const auto &inverse = old->operations[old->operation_ids.at(id)].inverse;
      Index back = 0;
      while (compiled.bindings.requests[back].id != inverse)
        ++back;
      const auto reversed = compiled.program.plan(back, t.after, tuples[back]);
      check(reversed.transition && reversed.transition->after == t.before,
            "Inverse completion failed: " + id);
      answer = {{"status", "Permitted"},
                {"transition",
                 {{"request", id},
                  {"choices", named(tuples[request])},
                  {"before", t.before},
                  {"after", t.after},
                  {"actions", wanted}}}};
    } else {
      check(planned.rejection == flat::Rejection::relation, "Blocked tuple reached target validation: " + id);
      answer = {{"status", "Rejected"}, {"reason", "relation"}};
    }
    if (export_case) {
      queries.push_back(
          {{"op", "plan"}, {"request", id}, {"state", to_flat(state)}, {"choices", named(tuples[request])}});
      expected.push_back(std::move(answer));
    }
  }
  void states() {
    const auto validate = [&](const State &state, bool export_case) {
      const auto valid = validate_state(*old, state).empty();
      check(compiled.program.valid(to_flat(state)) == valid, "Assignment validity differs");
      if (valid)
        check(compiled.program.solved(to_flat(state)) == solved(*old, state), "Goal differs");
      if (export_case) {
        queries.push_back({{"op", "validate"}, {"state", to_flat(state)}});
        expected.push_back({{"status", valid ? "Valid" : "Invalid"}});
      }
    };
    std::uint32_t serial = 0;
    for (Index p = 0; p < old->pieces.size(); ++p)
      for (Index q = 0; q < old->domains[old->pieces[p].domain].placements.size(); ++q) {
        auto state = old->initial;
        state.placement_of[p] = q;
        validate(state, (++serial % 97) == 0);
      }
    for (Index p = 0; p < compiled.bindings.pieces.size(); ++p)
      if (compiled.bindings.pieces[p].starts_with("mechanism/"))
        for (Index q = 384; q < 420; ++q) {
          auto state = compiled.program.data().initial;
          state[p] = q;
          const auto local = compiled.bindings.placements[q];
          const auto own = "mechanism/edge/" + compiled.bindings.pieces[p].substr(10) + "/";
          if (!local.starts_with(own))
            check(!compiled.program.valid(state), "Mechanism exchanged its intrinsic axis");
        }
    std::uint32_t random = 1024;
    auto next = [&] {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      return random;
    };
    for (int i = 0; i < 128; ++i) {
      auto state = old->initial;
      for (int n = 0; n < 2; ++n) {
        const auto p = next() % old->pieces.size();
        state.placement_of[p] = next() % old->domains[old->pieces[p].domain].placements.size();
      }
      validate(state, false);
    }
    auto state = old->initial;
    for (int step = 0; step < 33; ++step) {
      check(compiled.program.solved(to_flat(state)) == solved(*old, state), "Walk goal differs");
      for (Index r = 0; r < tuples.size(); ++r)
        compare(state, r, step < 3);
      std::vector<std::string> legal;
      for (const auto &r : legal_operations(*old, state))
        legal.push_back(r.at("operation").get<std::string>());
      state = twisty::plan(*old, state, legal[next() % legal.size()]).transition->after;
    }
    // Exercise every stop request at least once, including b/c source phases.
    for (Index r = 0; r < tuples.size(); ++r) {
      const auto id = compiled.bindings.requests[r].id;
      state = old->initial;
      if (id[id.size() - 2] != 'a') {
        const auto prefix = id.substr(0, 2) + "_a" + id[id.size() - 2];
        const auto turn = twisty::plan(*old, state, prefix);
        check(turn.transition.has_value(), "Source-phase setup failed");
        state = turn.transition->after;
      }
      compare(state, r, true);
      auto wrong = tuples[r];
      wrong[r % 3] = 0;
      check(!compiled.program.plan(r, to_flat(state), wrong).transition,
            "Incomplete local coupling accepted");
    }
    check(exercised.size() == 180, "Some directed stops were not exercised");
  }
  void sessions() {
    const auto package = read("relational-package.json").dump();
    Session session(package), previous(read("definition.json").dump());
    const auto origin = session.snapshot();
    check(origin.at("candidateRequests").size() == 60, "Source-phase availability changed");
    check(origin.at("legalRequests").size() == 60, "Initial legality changed");
    const auto first = session.plan_json(R"({"operation":"UF_ab","parameters":{}})", session.state_json());
    const auto first_state = Json::parse(first).at("transition").at("afterState").dump();
    const auto blocked =
        Json::parse(session.plan_json(R"({"operation":"UR_ad","parameters":{}})", first_state));
    check(blocked.at("reasonCode") == "relation.rejected" &&
              blocked.at("implicatedPieces") ==
                  Json({"center/Ufl", "center/Ufr", "corner/FLU", "mechanism/UF"}),
          "Rejection proof did not identify the actual jumble blockers");
    const auto result = session.run("UF_ab UL_af", "transactional", "0");
    check(result.at("status") == "Committed", "Jumble session failed");
    const auto legacy = previous.run("UF_ab UL_af", "transactional", "0");
    check(session.snapshot().at("state") == previous.snapshot().at("state"), "Session jumble differs");
    const auto definitions = session.execution()->definition_json();
    check(definitions.at("pieceTypes").size() == 3, "Inspector leaks twelve mechanism domains");
    const auto bound = session.snapshot().at("candidateRequests");
    check(bound.size() == 60 && std::any_of(bound.begin(), bound.end(),
                                            [](const auto &r) { return r.at("operation") == "UF_ba"; }),
          "Jumble phase candidates did not update");
    for (const auto *kind : {"euclidean", "port-diagram", "spherical"}) {
      auto realization = read(std::string("helicopter-") + kind + ".json");
      Geometry old_geometry(previous.execution(), realization.dump());
      realization["compatibleDefinitionDigest"] = session.definition()->digest;
      realization["modelByDomain"] = {{"center", "center"}, {"corner", "corner"}, {"mechanism", "edge"}};
      Geometry geometry(session.execution(), realization.dump());
      check(geometry.positions() == old_geometry.positions() &&
                geometry.indices() == old_geometry.indices() && geometry.normals() == old_geometry.normals(),
            "Interpreter meshes changed");
      for (Index i = 0; i < 2; ++i) {
        check(Json::parse(geometry.prepare_animation_json(result.at("transitions").at(i).dump()))
                      .at("status") == "Prepared",
              "New witness rejected by interpreter");
        check(Json::parse(old_geometry.prepare_animation_json(legacy.at("transitions").at(i).dump()))
                      .at("status") == "Prepared",
              "Legacy oracle track failed");
        for (const auto progress : {0., .125, .5, .875, 1.}) {
          geometry.sample(progress);
          old_geometry.sample(progress);
          check(geometry.transforms() == old_geometry.transforms(), "Sampled pose or winding differs");
        }
      }
      auto bad = result.at("transitions").at(0);
      bad["pieceActions"][0]["event"] = "wrong";
      check(Json::parse(geometry.prepare_animation_json(bad.dump())).at("status") == "UnsupportedTransition",
            "Tampered stop event animated");
    }
    const auto saved = session.save();
    session.undo(session.revision());
    check(session.redo(session.revision()).at("status") == "Committed", "Stop redo failed");
    check(session.load(saved).at("status") == "Loaded", "Completed stop replay failed");
    previous.undo(previous.revision());
    const auto conversion =
        Json::parse(session.convert_legacy_json(previous.save_json(), previous.definition_json()));
    check(conversion.at("status") == "Converted", "Legacy Helicopter save conversion failed");
    session.load(conversion.at("session"));
    check(session.snapshot().at("state") == previous.snapshot().at("state"), "Converted cursor differs");
    check(session.redo(session.revision()).at("status") == "Committed", "Converted redo tail failed");
    check(session.run("(UF_ab UL_af)'", "transactional", session.revision()).at("status") == "Committed",
          "Inverse notation failed");
    check(session.snapshot().at("state") == origin.at("state"), "Inverse did not restore origin");
    auto invalid = saved;
    invalid["logical"]["requests"][0]["choices"][0] = "idle";
    const auto before = session.snapshot();
    check(Json::parse(session.load_json(invalid.dump())).at("status") == "Invalid" &&
              session.snapshot() == before,
          "Failed load edited state");
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    const auto start = std::chrono::steady_clock::now();
    std::string emit;
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if ((option != "--source-dir" && option != "--emit") || i + 1 == argc)
        throw std::runtime_error("Use --source-dir <generated sources> or --emit <cases file>");
      (option == "--source-dir" ? source_directory : emit) = argv[++i];
    }
    Comparison test;
    test.tables();
    test.states();
    test.sessions();
    if (!emit.empty()) {
      std::ofstream file(emit);
      file << Json{{"source", test.source.dump()},
                   {"preview", language::inspect(test.compiled)},
                   {"queries", test.queries},
                   {"expected", test.expected}}
                  .dump();
    }
    std::cout << checks << " Helicopter relational checks passed in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " seconds\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
