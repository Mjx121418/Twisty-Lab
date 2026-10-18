#include "twisty/kernel.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using namespace twisty;
namespace language = twisty::language;
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
                             : std::string(PROJECT_ROOT) + "/packages/bagua";
  std::ifstream file(directory + "/" + name);
  if (!file)
    throw std::runtime_error("Missing Bagua fixture: " + name);
  return Json::parse(file);
}
template <class T> Index find(const std::vector<T> &values, const T &value) {
  const auto it = std::find(values.begin(), values.end(), value);
  if (it == values.end())
    throw std::runtime_error("Missing symbol: " + value);
  return static_cast<Index>(it - values.begin());
}
std::string group(const std::string &domain) {
  return domain;
}
struct Comparison {
  Json source = read("relational.pzl.json"), catalog = read("relational-bindings.json");
  language::CompiledPuzzle compiled = language::compile(source);
  std::shared_ptr<const Definition> old = load_definition(read("definition.json"));
  std::vector<Index> old_piece;
  std::vector<std::vector<Index>> placements;
  std::vector<core::Choices> tuples;
  Json queries = Json::array(), expected = Json::array();
  std::set<std::string> exercised;
  Comparison() {
    const auto &data = compiled.program.data();
    check(data.piece_count == 146 && data.placement_count == 2232 && data.requests.size() == 30,
          "Catalog size changed");
    check(compiled.bindings.choice_slots.size() == 8, "Expected eight authoring domains");
    check(data.predicates.size() < 100000 && data.maps.size() < 300, "Compact lowering regressed");
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
      core::Choices tuple;
      for (const auto &values : compiled.bindings.choice_values)
        tuple.push_back(find(values, r.id));
      tuples.push_back(std::move(tuple));
    }
    check(to_core(old->initial) == data.initial, "Initial identity mapping changed");
  }
  core::State to_core(const State &state) const {
    core::State result;
    for (const auto p : old_piece)
      result.push_back(placements[old->pieces[p].domain][state.placement_of[p]]);
    return result;
  }
  Json named(const core::Choices &tuple) const {
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
    const auto planned = compiled.program.plan(request, to_core(state), tuples[request]);
    check(planned.transition.has_value() == legacy.transition.has_value(), "Permission differs: " + id);
    Json answer;
    if (planned.transition) {
      exercised.insert(id);
      const auto &t = *planned.transition;
      check(t.after == to_core(legacy.transition->after), "Target differs: " + id);
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
      check(planned.rejection == core::Rejection::relation, "Blocked tuple reached target validation: " + id);
      answer = {{"status", "Rejected"}, {"reason", "relation"}};
    }
    if (export_case) {
      queries.push_back(
          {{"op", "plan"}, {"request", id}, {"state", to_core(state)}, {"choices", named(tuples[request])}});
      expected.push_back(std::move(answer));
    }
  }
  void states() {
    const auto validate = [&](const State &state, bool export_case) {
      const auto valid = validate_state(*old, state).empty();
      check(compiled.program.valid(to_core(state)) == valid, "Assignment validity differs");
      if (valid)
        check(compiled.program.solved(to_core(state)) == solved(*old, state), "Goal differs");
      if (export_case) {
        queries.push_back({{"op", "validate"}, {"state", to_core(state)}});
        expected.push_back({{"status", valid ? "Valid" : "Invalid"}});
      }
    };
    std::uint32_t serial = 0;
    std::set<Index> domains;
    for (Index p = 0; p < old->pieces.size(); ++p) {
      const auto domain = old->pieces[p].domain;
      const bool first = domains.insert(domain).second;
      for (Index q = 0; q < old->domains[domain].placements.size(); ++q) {
        // Exhaust the catalog for a representative identity in every family;
        // every other identity also exercises all home-cell orientations.
        if (!first && old->domains[domain].placements[q].footprint !=
                          old->domains[domain].placements[old->pieces[p].home].footprint)
          continue;
        auto state = old->initial;
        state.placement_of[p] = q;
        validate(state, (++serial % 37) == 0);
      }
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
      check(compiled.program.solved(to_core(state)) == solved(*old, state), "Walk goal differs");
      for (Index r = 0; r < tuples.size(); ++r)
        compare(state, r, step < 3);
      std::vector<std::string> legal;
      for (const auto &r : legal_operations(*old, state))
        legal.push_back(r.at("operation").get<std::string>());
      state = twisty::plan(*old, state, legal[next() % legal.size()]).transition->after;
    }
    // Every named request is legal at the origin, including half-turn aliases.
    for (Index r = 0; r < tuples.size(); ++r) {
      compare(old->initial, r, true);
      auto wrong = tuples[r];
      wrong[r % 8] = 0;
      check(!compiled.program.plan(r, to_core(old->initial), wrong).transition,
            "Incomplete domain tuple accepted");
    }
    check(exercised.size() == 30, "Some Bagua requests were not exercised");
  }
  void sessions() {
    const auto package = read("relational-package.json").dump();
    Session session(package), previous(read("definition.json").dump());
    const auto origin = session.snapshot();
    check(origin.at("candidateRequests").size() == 30, "Candidate set changed");
    const std::string algorithm = "U+ R' L' D2 R L U-";
    const auto result = session.run(algorithm, "transactional", "0");
    const auto legacy = previous.run(algorithm, "transactional", "0");
    check(result.at("status") == "Committed" && legacy.at("status") == "Committed",
          "Published sequence failed");
    check(session.snapshot().at("state") == previous.snapshot().at("state"), "Session jumble differs");
    for (const auto *kind : {"euclidean", "port-diagram", "spherical"}) {
      auto realization = read(std::string("bagua-") + kind + ".json");
      Geometry old_geometry(previous.execution(), realization.dump());
      realization["compatibleDefinitionDigest"] = session.definition()->digest;
      Geometry geometry(session.execution(), realization.dump());
      check(geometry.positions() == old_geometry.positions() &&
                geometry.indices() == old_geometry.indices() && geometry.normals() == old_geometry.normals(),
            "Interpreter meshes changed");
      for (Index i = 0; i < result.at("transitions").size(); ++i) {
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
    check(conversion.at("status") == "Converted", "Legacy Bagua save conversion failed");
    session.load(conversion.at("session"));
    check(session.snapshot().at("state") == previous.snapshot().at("state"), "Converted cursor differs");
    check(session.redo(session.revision()).at("status") == "Committed", "Converted redo tail failed");
    check(session.run("(" + algorithm + ")'", "transactional", session.revision()).at("status") ==
              "Committed",
          "Inverse notation failed");
    check(session.snapshot().at("state") == origin.at("state"), "Inverse did not restore origin");
    const auto blocked = session.run("U+ R F- U+", "transactional", session.revision());
    check(blocked.at("reasonCode") == "relation.rejected" &&
              std::find(blocked.at("implicatedPieces").begin(), blocked.at("implicatedPieces").end(),
                        Json("corner/07")) != blocked.at("implicatedPieces").end(),
          "Cross-domain obstruction lost its actual blocker");
    check(session.snapshot().at("state") == origin.at("state"), "Blocked transaction changed state");
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
    std::cout << checks << " Bagua relational checks passed in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " seconds\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
