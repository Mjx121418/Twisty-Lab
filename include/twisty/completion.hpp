#pragma once
#include "twisty/runtime.hpp"

namespace twisty::runtime {
// Selection policy and search live outside core::Program. A selected profile is
// still only a candidate; the executor must check it against the relation.
std::optional<core::Choices> bind_profile(const CompiledPuzzle &puzzle, core::Index request,
                                          std::span<const core::Index> state,
                                          const std::optional<std::string> &profile = {});
std::vector<std::optional<core::Choices>> bind_default_profiles(const CompiledPuzzle &,
                                                                std::span<const core::Index> state);
struct Discovery {
  std::vector<core::Choices> choices;
  std::uint32_t examined = 0;
  bool complete = false;
};
Discovery discover(const core::Program &program, core::Index request, std::span<const core::Index> state,
                   std::uint32_t candidate_limit, std::uint32_t result_limit);
// State inputs used by a sufficient Boolean proof of relation rejection. This
// is neither a minimum cause nor an unsatisfiability proof over other tuples.
std::vector<core::Index> rejection_inputs(const core::Program &, core::Index request,
                                          std::span<const core::Index> state,
                                          std::span<const core::Index> choices);
} // namespace twisty::runtime

// Preserve the source API without making runtime consumers include the compiler.
namespace twisty::language {
using runtime::bind_profile;
using runtime::bind_default_profiles;
using runtime::Discovery;
using runtime::discover;
using runtime::rejection_inputs;
} // namespace twisty::language
