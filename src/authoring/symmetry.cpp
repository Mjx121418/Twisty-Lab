#include "twisty/symmetry.hpp"
#include <algorithm>
#include <numeric>
#include <set>
#include <utility>

namespace twisty::symmetry {
Error::Error(std::string code, std::string source, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(code)), source(std::move(source)) {}
void validate_permutation(const Permutation &p) {
  auto sorted = p;
  std::sort(sorted.begin(), sorted.end());
  for (Index i = 0; i < sorted.size(); ++i)
    if (sorted[i] != i)
      throw Error("symmetry.permutation", "/group/generators", "Generator is not a permutation.");
}
Permutation compose(const Permutation &a, const Permutation &b) {
  if (a.size() != b.size())
    throw Error("symmetry.degree", "/group", "Permutation degrees differ.");
  validate_permutation(a);
  validate_permutation(b);
  Permutation result(a.size());
  for (Index i = 0; i < a.size(); ++i)
    result[i] = a[b[i]];
  return result;
}
Permutation inverse(const Permutation &p) {
  validate_permutation(p);
  Permutation result(p.size());
  for (Index i = 0; i < p.size(); ++i)
    result[p[i]] = i;
  return result;
}
std::vector<Permutation> enumerate_group(const std::vector<Permutation> &generators, std::size_t limit) {
  if (generators.empty() || generators[0].size() > 32 || limit == 0 || limit > 65536)
    throw Error("symmetry.limit", "/group",
                "Supply generators of degree at most 32 and a finite limit at most 65536.");
  for (const auto &g : generators) {
    validate_permutation(g);
    if (g.size() != generators[0].size())
      throw Error("symmetry.degree", "/group", "Generator degrees differ.");
  }
  Permutation identity(generators[0].size());
  std::iota(identity.begin(), identity.end(), 0);
  std::set<Permutation> seen{identity};
  std::vector<Permutation> queue{identity};
  for (std::size_t i = 0; i < queue.size(); ++i)
    for (const auto &generator : generators) {
      auto candidate = compose(generator, queue[i]);
      if (seen.insert(candidate).second) {
        if (seen.size() > limit)
          throw Error("symmetry.limit", "/group/limit",
                      "Finite group enumeration exceeded its declared limit.");
        queue.push_back(std::move(candidate));
      }
    }
  return {seen.begin(), seen.end()};
}
std::vector<std::vector<Permutation>> enumerate_cosets(const std::vector<Permutation> &group,
                                                       const std::vector<Permutation> &subgroup) {
  const std::set<Permutation> members(group.begin(), group.end());
  if (group.empty() || subgroup.empty())
    throw Error("symmetry.subgroup", "/group", "A coset domain requires a nonempty group and subgroup.");
  for (const auto &a : subgroup) {
    if (!members.contains(a))
      throw Error("symmetry.subgroup", "/group", "Subgroup element is outside the group.");
    for (const auto &b : subgroup)
      if (std::find(subgroup.begin(), subgroup.end(), compose(a, b)) == subgroup.end())
        throw Error("symmetry.subgroup", "/group", "Supplied subgroup is not closed.");
  }
  std::set<Permutation> remaining = members;
  std::vector<std::vector<Permutation>> cosets;
  while (!remaining.empty()) {
    const auto representative = *remaining.begin();
    std::vector<Permutation> coset;
    for (const auto &h : subgroup) {
      auto value = compose(representative, h);
      remaining.erase(value);
      coset.push_back(std::move(value));
    }
    std::sort(coset.begin(), coset.end());
    cosets.push_back(std::move(coset));
  }
  return cosets;
}

} // namespace twisty::symmetry
