#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace twisty::symmetry {
using Index = std::uint32_t;
using Permutation = std::vector<Index>;
struct Error : std::runtime_error {
  std::string code, source;
  Error(std::string code, std::string source, std::string message);
};
void validate_permutation(const Permutation &);
Permutation compose(const Permutation &, const Permutation &);
Permutation inverse(const Permutation &);
std::vector<Permutation> enumerate_group(const std::vector<Permutation> &, std::size_t limit = 4096);
std::vector<std::vector<Permutation>> enumerate_cosets(const std::vector<Permutation> &group,
                                                       const std::vector<Permutation> &subgroup);
} // namespace twisty::symmetry
