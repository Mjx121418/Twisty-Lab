#pragma once
#include "twisty/relational_compiler.hpp"

namespace twisty::relational {
// Selection policy and search live outside flat::Program. A selected profile is
// still only a candidate; the executor must check it against the relation.
std::optional<flat::Choices> bind_profile(const CompiledPuzzle &puzzle, flat::Index request,
                                          std::span<const flat::Index> state,
                                          const std::optional<std::string> &profile = {});
struct Discovery {
  std::vector<flat::Choices> choices;
  std::uint32_t examined = 0;
  bool complete = false;
};
Discovery discover(const flat::Program &program, flat::Index request, std::span<const flat::Index> state,
                   std::uint32_t candidate_limit, std::uint32_t result_limit);
} // namespace twisty::relational
