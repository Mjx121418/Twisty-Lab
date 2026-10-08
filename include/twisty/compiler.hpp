#pragma once
#include "twisty/core.hpp"
#include <array>

namespace twisty {
using Permutation = std::vector<Index>;
Permutation compose(const Permutation &, const Permutation &);
Permutation inverse(const Permutation &);
std::vector<Permutation> enumerate_group(const std::vector<Permutation> &, std::size_t limit = 4096);
std::vector<std::vector<Permutation>> enumerate_cosets(const std::vector<Permutation> &group,
                                                       const std::vector<Permutation> &subgroup);
Json compile_source(const Json &source);
Json compile_json(const std::string &source);
} // namespace twisty
