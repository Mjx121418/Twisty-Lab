#include "twisty/core.hpp"
#include "twisty/completion.hpp"
#include "twisty/authoring/cube.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using twisty::Index;
using twisty::Json;
namespace core = twisty::core;
namespace language = twisty::language;
std::uint64_t checks = 0;
void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
Json read(const std::string &relative) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + relative);
  if (!file)
    throw std::runtime_error("Missing fixture: " + relative);
  return Json::parse(file);
}
template <class T> Index find(const std::vector<T> &values, const T &value) {
  const auto it = std::find(values.begin(), values.end(), value);
  check(it != values.end(), "Missing symbol " + value);
  return static_cast<Index>(it - values.begin());
}
struct Comparison {
  language::CubeModule module;
  language::CompiledPuzzle compiled;
  std::shared_ptr<const twisty::Definition> legacy;
  std::vector<Index> old_piece;
  std::vector<std::vector<Index>> old_placement;
  Json suite;

  explicit Comparison(bool bandaged)
      : module(language::cube_module(bandaged)), compiled(language::compile(module.source)),
        legacy(twisty::load_definition(
            read(std::string("packages/") + (bandaged ? "bandaged" : "cube3") + "/definition.json"))) {
    const auto directory = std::string("packages/") + (bandaged ? "bandaged" : "cube3");
    check(module.source == read(directory + "/relational.pzl.json"), "Expanded AST is stale");
    check(module.catalog == read(directory + "/relational-bindings.json"), "Catalog bindings are stale");
    check(compiled.program.data().piece_count == (bandaged ? 25 : 26), "Persistent identities changed");
    check(compiled.program.data().placement_count == (bandaged ? 78 : 54), "Placement union changed");
    check(compiled.bindings.requests.size() == legacy->operations.size(), "Directed request catalog changed");
    std::vector<std::string> old_ids;
    for (const auto &p : legacy->pieces)
      old_ids.push_back(p.id);
    for (const auto &id : compiled.bindings.pieces)
      old_piece.push_back(find(old_ids, id));
    old_placement.resize(legacy->domains.size());
    for (Index d = 0; d < legacy->domains.size(); ++d)
      for (const auto &q : legacy->domains[d].placements)
        old_placement[d].push_back(find(compiled.bindings.placements, legacy->domains[d].id + "/" + q.key));
    check(to_core(legacy->initial) == compiled.program.data().initial, "Initial assignments differ");
    suite = {{"source", module.source.dump()},
             {"catalog", module.catalog},
             {"preview", language::inspect(compiled)},
             {"queries", Json::array()},
             {"expected", Json::array()}};
  }
  core::State to_core(const twisty::State &old) const {
    core::State result;
    for (auto p : old_piece)
      result.push_back(old_placement[legacy->pieces[p].domain][old.placement_of[p]]);
    return result;
  }
  Index request(const std::string &id) const {
    for (Index i = 0; i < compiled.bindings.requests.size(); ++i)
      if (compiled.bindings.requests[i].id == id)
        return i;
    throw std::runtime_error("Missing request " + id);
  }
  Json choices(const core::Choices &tuple) const {
    auto result = Json::array();
    for (Index d = 0; d < tuple.size(); ++d)
      result.push_back(compiled.bindings.choice_values[d][tuple[d]]);
    return result;
  }
  Json expected(const twisty::Plan &old, const twisty::State &state, const std::string &id,
                const core::Choices &tuple) const {
    if (!old.transition)
      return {{"status", "Rejected"}, {"reason", "relation"}};
    auto actions = Json::array();
    for (const auto &a : old.transition->actions) {
      const auto &piece = legacy->pieces[a.piece];
      const auto core_piece = find(compiled.bindings.pieces, piece.id);
      const auto event = legacy->operations[legacy->operation_ids.at(id)].transport;
      actions.push_back({{"piece", core_piece},
                         {"from", old_placement[piece.domain][a.from]},
                         {"to", old_placement[piece.domain][a.to]},
                         {"event", find(compiled.bindings.events, event)}});
    }
    // Family and piece ordering are metadata, so compare by stable identity.
    std::sort(actions.begin(), actions.end(), [](const Json &a, const Json &b) {
      return a.at("piece").get<Index>() < b.at("piece").get<Index>();
    });
    return {{"status", "Permitted"},
            {"transition",
             {{"request", id},
              {"choices", choices(tuple)},
              {"before", to_core(state)},
              {"after", to_core(old.transition->after)},
              {"actions", actions}}}};
  }
  void catalogs() const {
    check(module.catalog.at("resources").get<std::vector<std::string>>() == legacy->cells,
          "Exclusion resources differ");
    for (const auto &p : legacy->pieces) {
      const auto &binding = module.catalog.at("pieces").at(p.id);
      check(binding.at("portLabels").get<std::map<std::string, std::string>>() == p.labels,
            "Piece labels changed");
      check(binding.at("home").get<std::string>() ==
                legacy->domains[p.domain].id + "/" + legacy->domains[p.domain].placements[p.home].key,
            "Home binding changed");
    }
    for (const auto &d : legacy->domains)
      for (const auto &q : d.placements) {
        const auto &binding = module.catalog.at("placements").at(d.id + "/" + q.key);
        auto occupied = binding.at("resources").get<std::vector<std::string>>();
        std::sort(occupied.begin(), occupied.end());
        check(occupied == q.footprint, "Catalog incidence changed");
        for (const auto &[id, port] : q.ports) {
          check(binding.at("ports").at(id).at("cell").get<std::string>() == port.first,
                "Port resource changed");
          check(binding.at("ports").at(id).at("attachment").get<std::string>() == port.second,
                "Port attachment changed");
        }
      }
    // Exhaust every local placement, including sources outside the moving layer.
    for (const auto &op : legacy->operations) {
      const auto &binding = module.catalog.at("requests").at(op.id);
      check(binding.at("inverse").get<std::string>() == op.inverse &&
                binding.at("family").get<std::string>() == op.family &&
                binding.at("event").get<std::string>() == op.transport,
            "Request/event convention changed");
      std::set<std::string> selected(op.cells.begin(), op.cells.end());
      for (Index d = 0; d < legacy->domains.size(); ++d) {
        const auto &domain = legacy->domains[d];
        const auto &ast_domain = module.source.at("domains").at(d);
        check(ast_domain.at("id").get<std::string>() == domain.id, "Domain binding order differs");
        const auto &moves = ast_domain.at("moves");
        const auto move = std::find_if(moves.begin(), moves.end(),
                                       [&](const Json &x) { return x.at("id").get<std::string>() == op.id; });
        check(move != moves.end(), "Missing local movement");
        for (Index q = 0; q < domain.placements.size(); ++q) {
          const auto &placement = domain.placements[q];
          const auto count = std::count_if(placement.footprint.begin(), placement.footprint.end(),
                                           [&](const auto &c) { return selected.contains(c); });
          const bool active = count == static_cast<std::ptrdiff_t>(placement.footprint.size());
          check(move->at("transport").contains(placement.key) == active, "Local support changed");
          if (active)
            check(move->at("transport").at(placement.key).get<std::string>() ==
                      domain.placements[op.maps[d][q]].key,
                  "Exact transport changed");
        }
      }
    }
  }
  void state(const twisty::State &old, bool record) {
    const auto core_state = to_core(old);
    check(twisty::validate_state(*legacy, old).empty() && compiled.program.valid(core_state),
          "Sample must be valid");
    check(compiled.program.solved(core_state) == twisty::solved(*legacy, old), "Goal changed");
    if (record) {
      suite["queries"].push_back({{"op", "goal"}, {"state", core_state}});
      suite["expected"].push_back({{"status", "Evaluated"}, {"solved", twisty::solved(*legacy, old)}});
    }
    for (const auto &op : legacy->operations) {
      const auto r = request(op.id);
      const auto tuple = language::bind_profile(compiled, r, core_state);
      check(tuple.has_value(), "Fixed turn profile requires search");
      const auto old_plan = twisty::plan(*legacy, old, op.id);
      const auto actual = compiled.program.plan(r, core_state, *tuple);
      const auto want = expected(old_plan, old, op.id, *tuple);
      check(actual.transition.has_value() == old_plan.transition.has_value(),
            "Legal request changed: " + op.id);
      if (actual.transition) {
        check(actual.transition->after == to_core(old_plan.transition->after), "Endpoint changed");
        auto actual_actions = Json::array();
        for (const auto &a : actual.transition->actions)
          actual_actions.push_back({{"piece", a.piece}, {"from", a.from}, {"to", a.to}, {"event", a.event}});
        std::sort(actual_actions.begin(), actual_actions.end(), [](const Json &a, const Json &b) {
          return a.at("piece").get<Index>() < b.at("piece").get<Index>();
        });
        check(actual_actions == want.at("transition").at("actions"), "Participation/event witness changed");
        const auto inverse_request = request(op.inverse);
        const auto inverse_tuple =
            language::bind_profile(compiled, inverse_request, actual.transition->after);
        const auto inverse = compiled.program.plan(inverse_request, actual.transition->after, *inverse_tuple);
        check(inverse.transition && inverse.transition->after == core_state, "Inverse completion failed");
      }
      if (record) {
        suite["queries"].push_back(
            {{"op", "plan"}, {"state", core_state}, {"request", op.id}, {"choices", choices(*tuple)}});
        suite["expected"].push_back(want);
      }
    }
  }
  void rotations_and_walks() {
    const auto labels = module.catalog.at("faceLabels").get<std::vector<std::string>>();
    for (const auto &g : module.catalog.at("symmetry")) {
      auto rotated = legacy->initial;
      for (Index i = 0; i < legacy->pieces.size(); ++i) {
        const auto &p = legacy->pieces[i];
        const auto &domain = legacy->domains[p.domain];
        std::string target = domain.id + ":";
        for (const auto &[port, attachment] : domain.placements[p.home].ports) {
          (void)port;
          if (!target.ends_with(':'))
            target += '-';
          const auto index = find(labels, attachment.second);
          target += labels[g.at(index).get<Index>()];
        }
        rotated.placement_of[i] = domain.by_key.at(target);
      }
      state(rotated, true);
    }
    auto current = legacy->initial;
    std::uint32_t random = 0x179a4c2d;
    for (Index step = 0; step < 160; ++step) {
      state(current, step < 12);
      const auto legal = twisty::legal_operations(*legacy, current);
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      current =
          twisty::plan(*legacy, current, legal.at(random % legal.size()).at("operation")).transition->after;
    }
  }
  void invalid_and_completion_cases() {
    auto original = compiled.program.data().initial;
    // Reject every attempted substitution into a valid origin, or agree on the
    // validity of same-slot orientation changes. This also covers compound overlap.
    for (Index p = 0; p < legacy->pieces.size(); ++p) {
      const auto &piece = legacy->pieces[p];
      for (Index q = 0; q < legacy->domains[piece.domain].placements.size(); ++q) {
        auto changed = legacy->initial;
        changed.placement_of[p] = q;
        const bool valid = twisty::validate_state(*legacy, changed).empty();
        const auto numeric = to_core(changed);
        check(compiled.program.valid(numeric) == valid, "Assignment invariant changed");
        suite["queries"].push_back({{"op", "validate"}, {"state", numeric}});
        suite["expected"].push_back({{"status", valid ? "Valid" : "Invalid"}});
      }
    }
    for (Index p = 0; p < original.size(); ++p) {
      auto changed = original;
      changed[p] = original[(p + 1) % original.size()];
      check(!compiled.program.valid(changed), "A duplicate occupant or foreign catalog was accepted");
    }
    for (Index r = 0; r < compiled.bindings.requests.size(); ++r) {
      const auto correct = *language::bind_profile(compiled, r, original);
      for (Index d = 0; d < correct.size(); ++d) {
        for (Index choice = 0; choice < compiled.bindings.choice_values[d].size(); ++choice) {
          if (choice == correct[d])
            continue;
          auto wrong = correct;
          wrong[d] = choice;
          check(!compiled.program.plan(r, original, wrong).transition,
                "Request permitted an incomplete or unrelated tuple");
        }
      }
    }
    check(original == compiled.program.data().initial, "Checks changed the origin state");
  }
};
} // namespace

int main(int argc, char **argv) {
  try {
    auto suites = Json::array();
    for (bool bandaged : {false, true}) {
      Comparison comparison(bandaged);
      comparison.catalogs();
      comparison.rotations_and_walks();
      comparison.invalid_and_completion_cases();
      suites.push_back(std::move(comparison.suite));
    }
    if (argc == 3 && std::string(argv[1]) == "--emit") {
      std::ofstream output(argv[2]);
      output << suites.dump();
      if (!output)
        throw std::runtime_error("Cannot write migration cases");
    } else if (argc != 1)
      throw std::runtime_error("Usage: cube_language_tests [--emit path]");
    std::cout << checks << " cube/bandage migration checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
