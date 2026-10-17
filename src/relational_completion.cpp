#include "twisty/relational_completion.hpp"
#include <algorithm>

namespace twisty::relational {
std::optional<flat::Choices> bind_profile(const CompiledPuzzle &puzzle, flat::Index request,
                                          std::span<const flat::Index> state,
                                          const std::optional<std::string> &profile) {
  if (request >= puzzle.bindings.requests.size())
    throw flat::Error("flat.request", "Unknown request");
  if (!puzzle.program.valid(state))
    throw flat::Error("flat.state", "Source state violates the invariant");
  const auto &binding = puzzle.bindings.requests[request];
  std::optional<flat::Index> selected = binding.default_profile;
  if (profile) {
    const auto it = std::find_if(binding.profiles.begin(), binding.profiles.end(),
                                 [&](const auto &p) { return p.id == *profile; });
    if (it == binding.profiles.end())
      throw flat::Error("completion.profile", "Unknown profile");
    selected = static_cast<flat::Index>(it - binding.profiles.begin());
  }
  if (!selected)
    return {};
  const auto &candidate = binding.profiles[*selected];
  if (!puzzle.program.state_predicate(candidate.when, state))
    return {};
  return candidate.choices;
}

Discovery discover(const flat::Program &program, flat::Index request, std::span<const flat::Index> state,
                   std::uint32_t candidate_limit, std::uint32_t result_limit) {
  if (request >= program.data().requests.size())
    throw flat::Error("flat.request", "Unknown request");
  if (!program.valid(state))
    throw flat::Error("flat.state", "Source state violates the invariant");
  if (candidate_limit == 0 || result_limit == 0 || candidate_limit > 1000000 || result_limit > 10000)
    throw flat::Error("completion.limit", "Use 1..1000000 candidates and 1..10000 results");
  const auto &sizes = program.data().requests[request].choice_sizes;
  flat::Choices choices(sizes.size(), 0);
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
} // namespace twisty::relational
