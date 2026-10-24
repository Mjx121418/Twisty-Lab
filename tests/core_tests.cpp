#include "twisty/completion.hpp"
#include <iostream>
#include <map>

using namespace twisty::core;
using twisty::language::Json;
namespace {
int checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action, const std::string &code) {
  try {
    action();
  } catch (const Error &e) {
    check(e.code == code, ("Unexpected error: " + e.code).c_str());
    return;
  }
  throw std::runtime_error("Expected " + code);
}
ProgramData toggle() {
  ProgramData d;
  d.piece_count = 1;
  d.placement_count = 2;
  d.initial = {0};
  d.maps = {{{0, 1}, {1, 0}}};
  d.actions = {{0, 0, true_ref, 7}};
  d.requests = {{{2}, 0, 1, true_ref}};
  d.finite_sets = {{0}, {1}};
  Predicate at_home;
  at_home.input = 0;
  at_home.set = 0;
  d.predicates = {at_home};
  d.goal = 2;
  return d;
}
void execution() {
  Program p(toggle());
  const State source{0};
  const auto t = p.plan(0, source, Choices{1});
  check(t.transition && t.transition->after == State{1}, "Core transport failed");
  check(source == State{0}, "Planning changed caller state");
  check(t.transition->request == 0 && t.transition->choices == Choices{1} &&
            t.transition->actions[0].event == 7,
        "Witness lost completed request/event");
  check(p.solved(source) && !p.solved(t.transition->after), "Goal changed move permission");
  auto d = toggle();
  d.requests[0].relation = 2;
  Program source_condition(d);
  check(source_condition.plan(0, State{0}, Choices{0}).transition.has_value(),
        "Source test was re-evaluated on target");
  check(!source_condition.plan(0, State{1}, Choices{0}).transition, "Source relation was ignored");
  d = toggle();
  d.invariant = 2;
  Program constrained(d);
  const auto rejected = constrained.plan(0, source, Choices{0});
  check(rejected.rejection == Rejection::target_invariant && !rejected.transition,
        "Target invariant used cached source results");
  rejects([&] { constrained.plan(0, State{1}, Choices{0}); }, "core.state");
  rejects([&] { p.plan(0, State{0}, Choices{}); }, "core.choice");
  rejects([&] { p.plan(0, State{0}, Choices{2}); }, "core.choice");
  rejects([&] { p.plan(1, State{0}, Choices{0}); }, "core.request");
  rejects([&] { p.plan(0, State{2}, Choices{0}); }, "core.state");
  rejects([&] { p.valid(State{}); }, "core.state");
  d = toggle();
  d.maps[0] = {{0, 0}};
  const auto loop = Program(d).plan(0, State{0}, Choices{0});
  check(loop.transition && loop.transition->actions.size() == 1 &&
            loop.transition->before == loop.transition->after,
        "Self-loop lost participation");
  const auto absent = Program(d).plan(0, State{1}, Choices{0});
  check(absent.transition && absent.transition->actions.empty() && absent.transition->after == State{1},
        "Missing support row should be stationary");
  d = toggle();
  d.actions.push_back(d.actions.front());
  d.requests[0].action_count = 2;
  check(Program(d).plan(0, source, Choices{0}).rejection == Rejection::conflicting_writes,
        "Equal endpoints must not merge distinct writers/events");
  d.maps[0] = {{0, 1}};
  d.maps.push_back({{1, 0}});
  d.actions[1].map = 1;
  const auto parallel = Program(d).plan(0, source, Choices{0});
  check(parallel.transition && parallel.transition->after == State{1} &&
            parallel.transition->actions.size() == 1,
        "Parallel tables were applied sequentially");
}
void malformed_programs() {
  auto d = toggle();
  d.predicates[0].set = 8;
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.maps[0].push_back({1, 1});
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.maps[0] = {{0, 1}, {1, 1}};
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.requests[0].first_action = 100;
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.finite_sets[0] = {1, 0};
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.predicates[0].bank = Bank::choice;
  d.invariant = 2;
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.predicates[0].bank = Bank::choice;
  d.predicates[0].input = 1;
  d.goal = true_ref;
  d.requests[0].relation = 2;
  rejects([&] { Program bad(d); }, "core.program");
  // Shared graph validation must still check each distinct choice dimension.
  d = toggle();
  d.predicates[0].bank = Bank::choice;
  d.finite_sets[0] = {1};
  d.goal = true_ref;
  d.requests[0].relation = 2;
  d.requests.push_back({{1}, 0, 1, 2});
  rejects([&] { Program bad(d); }, "core.program");
  // Hundreds of requests may share a large action selection DAG. Its size is
  // within the finite budget even when request-by-request traversal is not.
  d = toggle();
  d.goal = true_ref;
  d.predicates.clear();
  d.finite_sets = {{1}};
  for (Index i = 0; i < 9000; ++i) {
    Predicate node;
    node.opcode = Opcode::range;
    node.child_count = 1;
    node.first_child = i;
    node.lower = 1;
    node.upper = 1;
    d.child_references.push_back(i + 1);
    d.predicates.push_back(node);
  }
  d.actions[0].selection = 9001;
  d.requests.assign(500, {{2}, 0, 1, true_ref});
  check(Program(d).plan(499, State{0}, Choices{1}).transition.has_value(),
        "Repeated requests exhausted validation on a shared dependency graph");
  d = toggle();
  d.predicates[0].bank = Bank::choice;
  d.finite_sets[0] = {2};
  d.goal = true_ref;
  d.requests[0].relation = 2;
  rejects([&] { Program bad(d); }, "core.program");
  d = toggle();
  d.predicates[0].opcode = Opcode::range;
  d.predicates[0].child_count = 1;
  d.predicates[0].upper = 1;
  d.child_references = {2};
  rejects([&] { Program bad(d); }, "core.program");
  // Deep graphs use an iterative evaluator, including on small native/WASM stacks.
  d = toggle();
  for (Index i = 0; i < 12000; ++i) {
    Predicate node;
    node.opcode = Opcode::range;
    node.child_count = 1;
    node.first_child = i;
    d.child_references.push_back(i + 2);
    d.predicates.push_back(node);
  }
  d.goal = 12002;
  check(Program(d).solved(State{0}), "Deep predicate graph failed");
}

void indexed_actions() {
  ProgramData d;
  d.piece_count = 180;
  d.placement_count = 3;
  d.initial.assign(d.piece_count, 0);
  d.maps = {{{0, 1}, {1, 2}, {2, 0}}, {{0, 0}, {2, 2}}};
  const auto member = [&](Bank bank, Index input, std::vector<Index> values) {
    Predicate node;
    node.bank = bank;
    node.input = input;
    node.set = static_cast<Index>(d.finite_sets.size());
    d.finite_sets.push_back(std::move(values));
    d.predicates.push_back(node);
    return static_cast<Ref>(d.predicates.size() + 1);
  };
  std::vector<Ref> selections;
  for (Index value = 0; value < 7; ++value)
    selections.push_back(member(Bank::choice, 0, {value}));
  for (Index piece = 0; piece < d.piece_count; ++piece)
    for (Index value = 0; value < 7; ++value)
      d.actions.push_back({piece, (piece + value) % 2, selections[value], piece * 10 + value});
  d.actions.push_back({0, 0, member(Bank::choice, 1, {1, 3}), 2000});
  d.actions.push_back({1, 0, member(Bank::state, 3, {2}), 2001});
  const auto phase = member(Bank::choice, 2, {0, 5, 10});
  const auto state = member(Bank::state, 9, {1});
  Predicate both;
  both.opcode = Opcode::range;
  both.lower = both.upper = both.child_count = 2;
  both.first_child = static_cast<Index>(d.child_references.size());
  d.child_references.insert(d.child_references.end(), {phase, state});
  d.predicates.push_back(both);
  d.actions.push_back({2, 1, static_cast<Ref>(d.predicates.size() + 1), 2002});
  d.actions.push_back({3, 1, true_ref, 2003});
  d.actions.push_back({4, 0, false_ref, 2004});
  d.requests = {{{7, 5, 11}, 0, static_cast<Index>(d.actions.size()), true_ref},
                {{7, 5, 11}, 20, 600, true_ref},
                {{7, 5, 11}, 31, 41, true_ref},
                {{}, static_cast<Index>(d.actions.size()), 0, true_ref}};
  Program indexed(d);
  // Equivalent RANGE wrappers force the general action evaluator. Compare
  // event order, sparse/self-loop participation, slices, and duplicate writers.
  std::map<Ref, Ref> wrapped;
  for (auto &action : d.actions) {
    if (action.selection < 2)
      continue;
    const auto node = d.predicates[action.selection - 2];
    if (node.opcode != Opcode::in || node.bank != Bank::choice)
      continue;
    if (!wrapped.contains(action.selection)) {
      Predicate wrapper;
      wrapper.opcode = Opcode::range;
      wrapper.lower = wrapper.upper = wrapper.child_count = 1;
      wrapper.first_child = static_cast<Index>(d.child_references.size());
      d.child_references.push_back(action.selection);
      d.predicates.push_back(wrapper);
      wrapped[action.selection] = static_cast<Ref>(d.predicates.size() + 1);
    }
    action.selection = wrapped.at(action.selection);
  }
  Program general(std::move(d));
  unsigned accepted = 0, conflicts = 0;
  for (Index sample = 0; sample < 5; ++sample) {
    State source(180);
    for (Index p = 0; p < source.size(); ++p)
      source[p] = (sample + p) % 3;
    for (Index request = 0; request < 4; ++request)
      for (Index choice = 0; choice < (request == 3 ? 1 : 7 * 5 * 11); ++choice) {
        const Choices tuple = request == 3 ? Choices{} : Choices{choice % 7, choice / 7 % 5, choice / 35};
        const auto a = indexed.plan(request, source, tuple), b = general.plan(request, source, tuple);
        check(a.rejection == b.rejection && a.transition.has_value() == b.transition.has_value(),
              "Indexed action permission differs");
        if (!a.transition) {
          conflicts += a.rejection == Rejection::conflicting_writes;
          continue;
        }
        ++accepted;
        const auto &x = *a.transition, &y = *b.transition;
        check(x.before == y.before && x.after == y.after && x.choices == y.choices &&
                  x.actions.size() == y.actions.size(),
              "Indexed action state or participation differs");
        for (Index i = 0; i < x.actions.size(); ++i)
          check(x.actions[i].piece == y.actions[i].piece && x.actions[i].from == y.actions[i].from &&
                    x.actions[i].to == y.actions[i].to && x.actions[i].event == y.actions[i].event,
                "Indexed action order or events differ");
      }
  }
  check(accepted > 0 && conflicts > 0, "Indexed action cases missed acceptance or writer conflicts");
}
Json toy() {
  return Json::parse(R"({"kind":"relational-puzzle","languageVersion":1,"id":"toy",
    "domains":[{"id":"d","placements":["0","1"],"pieces":[{"id":"p","initial":"0"}],
    "moves":[{"id":"flip","support":["0","1"],"transport":{"0":"1","1":"0"}}]}],
    "requests":[{"id":"u","relation":true,"profiles":[{"id":"idle","choices":{"d":"idle"}},
    {"id":"turn","choices":{"d":"flip"}}]}]})");
}
void language_and_completion() {
  using namespace twisty::language;
  auto source = toy();
  auto compiled = compile(source);
  check(!bind_profile(compiled, 0, State{0}), "No implicit first-solution/default policy is allowed");
  check(bind_profile(compiled, 0, State{0}, "turn") == Choices{1}, "Explicit profile failed");
  auto found = discover(compiled.program, 0, State{0}, 1, 10);
  check(found.examined == 1 && found.choices.size() == 1 && !found.complete,
        "Truncated discovery claimed completeness");
  found = discover(compiled.program, 0, State{0}, 100, 10);
  check(found.examined == 2 && found.choices.size() == 2 && found.complete, "Discovery missed idle or turn");
  found = discover(compiled.program, 0, State{0}, 100, 1);
  check(!found.complete, "Result cap must also mark incomplete search");
  rejects([&] { discover(compiled.program, 0, State{0}, 0, 1); }, "completion.limit");
  source["requests"][0]["relation"] = {{"chosen", {{"domain", "d"}, {"moves", {"flip"}}}}};
  source["requests"][0]["defaultProfile"] = "idle";
  compiled = compile(source);
  const auto selected = bind_profile(compiled, 0, State{0});
  check(selected == Choices{0} && !compiled.program.plan(0, State{0}, *selected).transition,
        "Selection must not change the relation to permit its default");
  found = discover(compiled.program, 0, State{0}, 1, 10);
  check(found.choices.empty() && !found.complete, "Failed candidate must not prove no completion exists");
  found = discover(compiled.program, 0, State{0}, 2, 10);
  check(found.choices == std::vector<Choices>{{1}} && found.complete, "Alternative completion was lost");
  source["requests"][0]["relation"] = false;
  compiled = compile(source);
  found = discover(compiled.program, 0, State{0}, 2, 10);
  check(found.choices.empty() && found.complete, "Exhaustive empty relation not reported");
  source = toy();
  source["requests"][0]["profiles"][1]["when"] = false;
  compiled = compile(source);
  check(!bind_profile(compiled, 0, State{0}, "turn"), "Profile condition was ignored");
  auto bad = toy();
  bad["domains"][0]["moves"][0]["transport"].erase("1");
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["domains"][0]["moves"][0]["transport"]["0"] = "0";
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["goal"] = {{"all", {false, {{"chosen", {{"domain", "d"}, {"moves", {"flip"}}}}}}}};
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["predicates"] = {{"a", {{"ref", "b"}}}, {"b", {{"ref", "a"}}}};
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["requests"][0]["profiles"][0]["choices"] = Json::object();
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["domains"][0]["pieces"].push_back(bad["domains"][0]["pieces"][0]);
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["languageVersion"] = 1.0;
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["requests"] = Json::array();
  bad["domains"][0]["moves"][0]["requires"] = {{"chosen", {{"domain", "d"}, {"moves", {"flip"}}}}};
  rejects([&] { compile(bad); }, "language.invalid");
  bad = toy();
  bad["invariant"] = false;
  rejects([&] { compile(bad); }, "core.initial");
  Puzzle api(toy().dump());
  check(Json::parse(api.query_json(R"({"op":"plan","state":[0],"request":"u","choices":[]})"))["status"] ==
            "Invalid",
        "JSON boundary accepted an incomplete tuple");
  check(Json::parse(api.query_json(
            R"({"op":"plan","state":[0.5],"request":"u","choices":["idle"]})"))["status"] == "Invalid",
        "Exact state API accepted a floating placement");
}
void shared_named_predicates() {
  using namespace twisty::language;
  auto source = toy();
  source["predicates"] = {{"leaf", {{"at", {{"piece", "p"}, {"placements", {"d/0"}}}}}}};
  std::string previous = "leaf";
  for (int level = 0; level < 6; ++level) {
    Json children = Json::array();
    for (int child = 0; child < 32; ++child)
      children.push_back({{"ref", previous}});
    const auto id = "level" + std::to_string(level);
    source["predicates"][id] = {{"any", children}};
    previous = id;
  }
  source["goal"] = {{"ref", previous}};
  source["requests"][0]["relation"] = {{"ref", previous}};
  auto compiled = compile(source);
  check(compiled.program.solved(State{0}) && !compiled.program.solved(State{1}) &&
            compiled.program.data().predicates.size() < 16,
        "Shared predicate DAG was expanded as a tree");
  source = toy();
  source["predicates"] = {
      {"conditional",
       {{"implies", Json::array({Json{{"request", {"u"}}},
                                 Json{{"chosen", {{"domain", "d"}, {"moves", {"flip"}}}}}})}}}};
  source["requests"][0]["relation"] = {{"ref", "conditional"}};
  auto second = source["requests"][0];
  second["id"] = "v";
  source["requests"].push_back(second);
  compiled = compile(source);
  check(!compiled.program.plan(0, State{0}, Choices{0}).transition &&
            compiled.program.plan(1, State{0}, Choices{0}).transition.has_value(),
        "Named request predicate lost specialization");
  source = toy();
  source["predicates"] = {{"choice", {{"chosen", {{"domain", "d"}, {"moves", {"flip"}}}}}}};
  source["requests"][0]["relation"] = {{"ref", "choice"}};
  source["requests"][0]["profiles"][0]["when"] = {{"ref", "choice"}};
  rejects([&] { compile(source); }, "language.invalid");
  source = toy();
  source["predicates"] = Json::object();
  previous = "p000";
  source["predicates"][previous] = true;
  for (int i = 1; i < 140; ++i) {
    auto id = "p" + std::to_string(1000 + i).substr(1);
    source["predicates"][id] = {{"ref", previous}};
    previous = id;
  }
  rejects([&] { compile(source); }, "language.invalid");
}
void rejection_proofs() {
  using namespace twisty::language;
  auto source = toy();
  source["domains"][0]["pieces"].push_back({{"id", "other"}, {"initial", "1"}});
  const Json home = {{"at", {{"piece", "p"}, {"placements", {"d/0"}}}}};
  const Json other = {{"at", {{"piece", "other"}, {"placements", {"d/1"}}}}};
  const Json chosen = {{"chosen", {{"domain", "d"}, {"moves", {"flip"}}}}};
  source["requests"][0]["relation"] = {{"all", {{{"not", home}}, {{"implies", {chosen, {{"not", other}}}}}}}};
  auto compiled = compile(source);
  check(rejection_inputs(compiled.program, 0, State{0, 1}, Choices{0}) == std::vector<Index>{0},
        "Rejection proof included an inactive interaction");
  check(rejection_inputs(compiled.program, 0, State{0, 1}, Choices{1}) == std::vector<Index>({0, 1}),
        "Rejection proof missed simultaneous blockers");
  check(rejection_inputs(compiled.program, 0, State{1, 0}, Choices{1}).empty(),
        "Accepted relation reported blockers");
  source["requests"][0]["relation"] = {{"count", {{"min", 0}, {"max", 1}, {"of", {home, home}}}}};
  compiled = compile(source);
  check(!compiled.program.plan(0, State{0, 1}, Choices{0}).transition &&
            rejection_inputs(compiled.program, 0, State{0, 1}, Choices{0}) == std::vector<Index>{0},
        "Rejection proof lost repeated-child multiplicity");
  const auto trace =
      compiled.program.predicate_trace(compiled.program.data().requests[0].relation, State{0, 1}, Choices{0});
  check(trace.at(compiled.program.data().requests[0].relation) == 0,
        "Predicate trace disagrees with planning");
  rejects([&] { compiled.program.predicate_trace(9999, State{0, 1}, Choices{0}); }, "core.input");
  rejects([&] { compiled.program.predicate_trace(true_ref, State{0}); }, "core.state");
  source["requests"][0]["relation"] = {{"any", {home, other, chosen}}};
  compiled = compile(source);
  const auto complete_trace =
      compiled.program.predicate_trace(compiled.program.data().requests[0].relation, State{0, 1}, Choices{1});
  check(std::count(complete_trace.begin(), complete_trace.end(), 1) >= 5,
        "Short-circuit evaluation omitted diagnostic branches");
  rejects(
      [&] {
        compiled.program.state_predicate(compiled.program.data().requests[0].relation, State{0, 1});
      },
      "core.input");
}
void bounded_json() {
  using namespace twisty::language;
  auto source = toy();
  const auto id = std::string(300, '[') + R"({ "quoted\" \\ } ])";
  source["id"] = id;
  check(compile_text(source.dump()).bindings.puzzle_id == id,
        "Quoted brackets or escapes changed JSON depth tracking");
  const auto deep = std::string(300, '[') + "0" + std::string(300, ']');
  rejects([&] { compile_text(deep); }, "language.limit");
  rejects([&] { compile_text(R"({"id":"unterminated)"); }, "language.invalid");
  source = toy();
  Json predicate = {{"all", Json::array()}};
  for (int i = 1; i < 127; ++i)
    predicate = {{"all", Json::array({predicate})}};
  source["requests"][0]["relation"] = predicate;
  check(compile_text(source.dump()).program.plan(0, State{0}, Choices{0}).transition.has_value(),
        "Nesting boundary changed for an empty deepest container");
  Puzzle puzzle(toy().dump());
  const auto rejected = Json::parse(puzzle.query_json(deep));
  check(rejected.at("status") == "Invalid" && rejected.at("diagnostics")[0].at("code") == "core.limit",
        "Query nesting limit was lost");
}
} // namespace
int main() {
  try {
    execution();
    malformed_programs();
    indexed_actions();
    language_and_completion();
    rejection_proofs();
    shared_named_predicates();
    bounded_json();
    std::cout << checks << " flat-core/compiler/completion checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
