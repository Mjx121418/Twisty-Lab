#include "twisty/completion.hpp"
#include <algorithm>
#include <set>

namespace twisty::runtime {
namespace {
std::optional<core::Choices> bound(const CompiledPuzzle &puzzle, const Profile &candidate,
                                   std::span<const core::Index> state) {
  if (candidate.when == core::false_ref)
    return {};
  if (candidate.when != core::true_ref &&
      puzzle.program.predicate_trace(candidate.when, state)[candidate.when] != 1)
    return {};
  return candidate.choices;
}
} // namespace
std::optional<core::Choices> bind_profile(const CompiledPuzzle &puzzle, core::Index request,
                                          std::span<const core::Index> state,
                                          const std::optional<std::string> &profile) {
  if (request >= puzzle.bindings.requests.size())
    throw core::Error("core.request", "Unknown request");
  if (!puzzle.program.valid(state))
    throw core::Error("core.state", "Source state violates the invariant");
  const auto &binding = puzzle.bindings.requests[request];
  std::optional<core::Index> selected = binding.default_profile;
  if (profile) {
    const auto it = std::find_if(binding.profiles.begin(), binding.profiles.end(),
                                 [&](const auto &p) { return p.id == *profile; });
    if (it == binding.profiles.end())
      throw core::Error("completion.profile", "Unknown profile");
    selected = static_cast<core::Index>(it - binding.profiles.begin());
  }
  if (!selected)
    return {};
  const auto &candidate = binding.profiles[*selected];
  return bound(puzzle, candidate, state);
}

std::vector<std::optional<core::Choices>> bind_default_profiles(const CompiledPuzzle &puzzle,
                                                                std::span<const core::Index> state) {
  if (!puzzle.program.valid(state))
    throw core::Error("core.state", "Source state violates the invariant");
  std::vector<std::optional<core::Choices>> result;
  for (const auto &request : puzzle.bindings.requests)
    result.push_back(request.default_profile
                         ? bound(puzzle, request.profiles[*request.default_profile], state)
                         : std::optional<core::Choices>{});
  return result;
}

Discovery discover(const core::Program &program, core::Index request, std::span<const core::Index> state,
                   std::uint32_t candidate_limit, std::uint32_t result_limit) {
  if (request >= program.data().requests.size())
    throw core::Error("core.request", "Unknown request");
  if (!program.valid(state))
    throw core::Error("core.state", "Source state violates the invariant");
  if (candidate_limit == 0 || result_limit == 0 || candidate_limit > 1000000 || result_limit > 10000)
    throw core::Error("completion.limit", "Use 1..1000000 candidates and 1..10000 results");
  const auto &sizes = program.data().requests[request].choice_sizes;
  core::Choices choices(sizes.size(), 0);
  Discovery result;
  while (result.examined < candidate_limit && result.choices.size() < result_limit) {
    ++result.examined;
    if (program.plan(request, state, choices).transition)
      result.choices.push_back(choices);
    // Streaming enumeration; never materialize the product or compute its size.
    bool advanced = false;
    for (std::size_t i = choices.size(); i > 0; --i) {
      if (++choices[i - 1] < sizes[i - 1]) {
        advanced = true;
        break;
      }
      choices[i - 1] = 0;
    }
    if (!advanced) {
      result.complete = true;
      break;
    }
  }
  return result;
}
std::vector<core::Index> rejection_inputs(const core::Program &program, core::Index request,
                                          std::span<const core::Index> state,
                                          std::span<const core::Index> choices) {
  const auto &data = program.data();
  if (request >= data.requests.size())
    throw core::Error("core.request", "Unknown request");
  const auto root = data.requests[request].relation;
  const auto values = program.predicate_trace(root, state, choices);
  if (values[root] != 0)
    return {};
  std::vector<core::Ref> pending{root};
  std::vector<bool> visited(values.size(), false);
  std::set<core::Index> inputs;
  while (!pending.empty()) {
    const auto ref = pending.back();
    pending.pop_back();
    if (ref < 2 || visited[ref])
      continue;
    visited[ref] = true;
    const auto &node = data.predicates[ref - 2];
    if (node.opcode == core::Opcode::in) {
      if (node.bank == core::Bank::state)
        inputs.insert(node.input);
      continue;
    }
    core::Index count = 0;
    for (core::Index i = 0; i < node.child_count; ++i)
      count += values[data.child_references[node.first_child + i]] == 1;
    for (core::Index i = 0; i < node.child_count; ++i) {
      const auto child = data.child_references[node.first_child + i];
      const bool truth = values[child] == 1;
      // A failed lower/upper bound needs false/true children respectively.
      // A satisfied bound needs its positive and negative evidence only where
      // that bound is nontrivial. Preserve multiplicity when counting above.
      const bool needed = values[ref] == 0 ? (count < node.lower ? !truth : truth)
                                           : (truth ? node.lower > 0 : node.upper < node.child_count);
      if (needed)
        pending.push_back(child);
    }
  }
  return {inputs.begin(), inputs.end()};
}
} // namespace twisty::runtime
