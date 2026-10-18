#include "twisty/core.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace twisty::core {
Error::Error(std::string value, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(value)) {}
namespace {
void require(bool condition, const char *code, const char *message) {
  if (!condition)
    throw Error(code, message);
}
bool slice(Index first, Index count, std::size_t size) {
  return first <= size && count <= size - first;
}

class Evaluation {
  const ProgramData &data;
  std::span<const Index> state, choices;
  std::vector<std::int8_t> values;
  bool exhaustive = false;
  struct Frame {
    Ref ref;
    Index next = 0, count = 0;
  };

public:
  Evaluation(const ProgramData &data, std::span<const Index> state, std::span<const Index> choices)
      : data(data), state(state), choices(choices), values(data.predicates.size() + 2, -1) {
    values[false_ref] = 0;
    values[true_ref] = 1;
  }
  bool get(Ref root) {
    if (values[root] != -1)
      return values[root];
    std::vector<Frame> stack{{root}};
    while (!stack.empty()) {
      auto &frame = stack.back();
      const auto &node = data.predicates[frame.ref - 2];
      if (node.opcode == Opcode::in) {
        const auto inputs = node.bank == Bank::state ? state : choices;
        require(node.input < inputs.size(), "core.input", "Predicate input is unavailable");
        const auto &set = data.finite_sets[node.set];
        values[frame.ref] = std::binary_search(set.begin(), set.end(), inputs[node.input]);
      } else if (!exhaustive &&
                 (frame.count > node.upper || frame.count + node.child_count - frame.next < node.lower)) {
        values[frame.ref] = 0;
      } else if (!exhaustive && frame.count >= node.lower &&
                 frame.count + node.child_count - frame.next <= node.upper) {
        values[frame.ref] = 1;
      } else if (frame.next < node.child_count) {
        const auto child = data.child_references[node.first_child + frame.next];
        if (values[child] == -1) {
          stack.push_back({child});
          continue;
        }
        frame.count += values[child] != 0;
        ++frame.next;
        continue;
      } else {
        values[frame.ref] = node.lower <= frame.count && frame.count <= node.upper;
      }
      stack.pop_back();
    }
    return values[root];
  }
  std::vector<std::int8_t> trace(Ref root) {
    // Diagnostic proofs need every reachable value, including branches whose
    // truth cannot affect the root. Normal permission checks may stop early.
    exhaustive = true;
    get(root);
    return std::move(values);
  }
};
} // namespace

Program::Program(ProgramData data) : data_(std::move(data)) {
  const auto &d = data_;
  require(d.piece_count <= 4096 && d.placement_count > 0 && d.placement_count <= 1000000 &&
              d.predicates.size() <= 1000000 && d.child_references.size() <= 4000000 &&
              d.finite_sets.size() <= 1000000 && d.maps.size() <= 1000000 && d.actions.size() <= 1000000 &&
              d.requests.size() <= 4096,
          "core.limit", "Program exceeds the finite execution limits");
  const auto ref_ok = [&](Ref ref) { return ref < d.predicates.size() + 2; };
  require(ref_ok(d.invariant) && ref_ok(d.goal), "core.program", "Invalid state predicate root");
  std::size_t set_values = 0, map_rows = 0;
  for (const auto &set : d.finite_sets) {
    set_values += set.size();
    require(set_values <= 4000000, "core.limit", "Too many finite-set entries");
    require(std::adjacent_find(set.begin(), set.end(), std::greater_equal<Index>()) == set.end(),
            "core.program", "Finite sets must be sorted and unique");
  }
  // A topological order makes cycle detection and state-only dependency checks linear.
  std::vector<bool> uses_choices(d.predicates.size() + 2, false);
  for (std::size_t i = 0; i < d.predicates.size(); ++i) {
    const auto &node = d.predicates[i];
    if (node.opcode == Opcode::in) {
      require(node.set < d.finite_sets.size(), "core.program", "Invalid finite-set reference");
      require(node.bank == Bank::state || node.bank == Bank::choice, "core.program", "Unknown input bank");
      require(node.input < (node.bank == Bank::state ? d.piece_count : 4096), "core.program",
              "Invalid input reference");
      if (node.bank == Bank::state && !d.finite_sets[node.set].empty())
        require(d.finite_sets[node.set].back() < d.placement_count, "core.program",
                "State set contains an unknown placement");
      uses_choices[i + 2] = node.bank == Bank::choice;
    } else {
      require(node.opcode == Opcode::range && node.lower <= node.upper && node.upper <= node.child_count &&
                  slice(node.first_child, node.child_count, d.child_references.size()),
              "core.program", "Invalid RANGE instruction");
      for (Index j = 0; j < node.child_count; ++j) {
        const auto child = d.child_references[node.first_child + j];
        require(child < i + 2, "core.program", "Predicates must be topologically ordered");
        uses_choices[i + 2] = uses_choices[i + 2] || uses_choices[child];
      }
    }
  }
  require(!uses_choices[d.invariant] && !uses_choices[d.goal], "core.program",
          "Invariant and goal must be state-only predicates");
  for (const auto &map : d.maps) {
    map_rows += map.size();
    require(map_rows <= 2000000, "core.limit", "Too many transport rows");
    std::vector<Index> targets;
    for (std::size_t i = 0; i < map.size(); ++i) {
      require(map[i].from < d.placement_count && map[i].to < d.placement_count &&
                  (i == 0 || map[i - 1].from < map[i].from),
              "core.program", "Transport rows must be known, sorted and unique");
      targets.push_back(map[i].to);
    }
    std::sort(targets.begin(), targets.end());
    require(std::adjacent_find(targets.begin(), targets.end()) == targets.end(), "core.program",
            "Transport must be one-to-one on its support");
  }
  for (const auto &action : d.actions)
    require(action.piece < d.piece_count && action.map < d.maps.size() && ref_ok(action.selection),
            "core.program", "Invalid action record");
  std::vector<Index> stamps(d.predicates.size() + 2, 0);
  std::size_t visits = 0;
  for (std::size_t i = 0; i < d.requests.size(); ++i) {
    const auto &request = d.requests[i];
    require(request.choice_sizes.size() <= 4096 && ref_ok(request.relation) &&
                slice(request.first_action, request.action_count, d.actions.size()),
            "core.program", "Invalid request program");
    for (auto size : request.choice_sizes)
      require(size > 0 && size <= 1000000, "core.program", "Invalid finite choice size");
    std::vector<Ref> pending{request.relation};
    for (Index a = 0; a < request.action_count; ++a)
      pending.push_back(d.actions[request.first_action + a].selection);
    while (!pending.empty()) {
      const auto ref = pending.back();
      pending.pop_back();
      if (ref < 2 || stamps[ref] == i + 1)
        continue;
      stamps[ref] = static_cast<Index>(i + 1);
      require(++visits <= 4000000, "core.limit", "Request dependency validation exceeds limit");
      const auto &node = d.predicates[ref - 2];
      if (node.opcode == Opcode::in && node.bank == Bank::choice) {
        require(node.input < request.choice_sizes.size(), "core.program", "Choice input is unavailable");
        const auto &set = d.finite_sets[node.set];
        require(set.empty() || set.back() < request.choice_sizes[node.input], "core.program",
                "Choice set contains an unknown value");
      } else if (node.opcode == Opcode::range) {
        for (Index j = 0; j < node.child_count; ++j)
          pending.push_back(d.child_references[node.first_child + j]);
      }
    }
  }
  require(valid(d.initial), "core.initial", "Initial state violates the invariant");
}

void Program::check_state_shape(std::span<const Index> state) const {
  require(state.size() == data_.piece_count, "core.state", "Wrong number of piece placements");
  for (auto q : state)
    require(q < data_.placement_count, "core.state", "Unknown placement index");
}
bool Program::valid(std::span<const Index> state) const {
  check_state_shape(state);
  return Evaluation(data_, state, {}).get(data_.invariant);
}
bool Program::solved(std::span<const Index> state) const {
  require(valid(state), "core.state", "Source state violates the invariant");
  return Evaluation(data_, state, {}).get(data_.goal);
}
bool Program::state_predicate(Ref root, std::span<const Index> state) const {
  // A public state-only predicate is useful to the separate profile binder.
  require(root < data_.predicates.size() + 2, "core.input", "Unknown predicate root");
  require(valid(state), "core.state", "Source state violates the invariant");
  return Evaluation(data_, state, {}).trace(root)[root] == 1;
}
std::vector<std::int8_t> Program::predicate_trace(Ref root, std::span<const Index> state,
                                                  std::span<const Index> choices) const {
  require(root < data_.predicates.size() + 2, "core.input", "Unknown predicate root");
  check_state_shape(state);
  require(choices.size() <= 4096, "core.input", "Too many choice inputs");
  return Evaluation(data_, state, choices).trace(root);
}
Plan Program::plan(Index request_id, std::span<const Index> state, std::span<const Index> choices) const {
  require(request_id < data_.requests.size(), "core.request", "Unknown request index");
  const auto &request = data_.requests[request_id];
  require(choices.size() == request.choice_sizes.size(), "core.choice", "A complete tuple is required");
  for (std::size_t i = 0; i < choices.size(); ++i)
    require(choices[i] < request.choice_sizes[i], "core.choice", "Unknown movement choice");
  check_state_shape(state);
  Evaluation source(data_, state, choices);
  require(source.get(data_.invariant), "core.state", "Source state violates the invariant");
  if (!source.get(request.relation))
    return {{}, Rejection::relation};
  Transition transition{request_id,
                        {choices.begin(), choices.end()},
                        {state.begin(), state.end()},
                        {state.begin(), state.end()},
                        {}};
  std::vector<bool> written(data_.piece_count, false);
  for (Index i = 0; i < request.action_count; ++i) {
    const auto &action = data_.actions[request.first_action + i];
    if (!source.get(action.selection))
      continue;
    const auto from = state[action.piece];
    const auto &map = data_.maps[action.map];
    const auto row =
        std::lower_bound(map.begin(), map.end(), from, [](const Row &r, Index q) { return r.from < q; });
    if (row == map.end() || row->from != from)
      continue;
    // Defense for hand-constructed programs, even when compiler predicates exclude conflicts.
    if (written[action.piece])
      return {{}, Rejection::conflicting_writes};
    written[action.piece] = true;
    transition.after[action.piece] = row->to;
    transition.actions.push_back({action.piece, from, row->to, action.event});
  }
  if (!Evaluation(data_, transition.after, {}).get(data_.invariant))
    return {{}, Rejection::target_invariant};
  return {std::move(transition), Rejection::none};
}
} // namespace twisty::core
