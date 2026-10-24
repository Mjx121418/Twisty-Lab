#include "twisty/core.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <numeric>

using namespace twisty::core;
namespace {
void check(bool x) {
  if (!x)
    throw std::runtime_error("Action test failed");
}
template <class F> void rejects(F f) {
  try {
    f();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error("Accepted malformed action data");
}
ProgramData fixture() {
  ProgramData d;
  std::vector<std::array<Index, 3>> perms;
  std::array<Index, 3> p{0, 1, 2};
  do {
    perms.push_back(p);
  } while (std::next_permutation(p.begin(), p.end()));
  Group g;
  g.order = 6;
  g.inverses.resize(6);
  for (Index a = 0; a < 6; ++a)
    for (Index b = 0; b < 6; ++b) {
      std::array<Index, 3> c;
      for (Index q = 0; q < 3; ++q)
        c[q] = perms[a][perms[b][q]];
      auto i = static_cast<Index>(std::find(perms.begin(), perms.end(), c) - perms.begin());
      g.products.push_back(i);
      if (i == 0)
        g.inverses[a] = b;
    }
  d.groups.push_back(g);
  d.finite_actions.push_back({0, 6, ActionKind::regular, {}});
  FiniteAction a{0, 3, ActionKind::tabulated, {}};
  for (const auto &p : perms)
    a.table.insert(a.table.end(), p.begin(), p.end());
  d.finite_actions.push_back(a);
  d.finite_actions.push_back({0, 1, ActionKind::trivial, {}});
  d.placement_count = 10;
  d.piece_count = 1;
  d.initial = {6};
  d.collections = {{0, {0, 1, 2, 3, 4, 5}}, {1, {6, 7, 8}}, {2, {9}}};
  d.reference_spaces = {{3}};
  d.permutations = {{0, {{0, 0}, {1, 1}}}, {0, {{0, 1}, {1, 2}}}, {0, {{0, 2}, {1, 0}}}};
  PartialPermutation derived;
  derived.space = 0;
  derived.base = 1;
  derived.output_action = 1;
  derived.output_element = 1;
  d.permutations.push_back(derived);
  d.reference_bindings = {{0, {6, 7, 8}}};
  ReferenceBinding b;
  b.space = 0;
  b.base = 0;
  b.group = 0;
  b.element = 2;
  d.reference_bindings.push_back(b);
  d.twist_families = {{0, {0, 1, 2, 3}}};
  d.maps.resize(4);
  d.transports.resize(4);
  d.transports[0].kind = TransportKind::difference;
  d.transports[0].source = 1;
  d.transports[0].target = 2;
  d.transports[1] = d.transports[0];
  d.transports[1].binding = 1;
  d.transports[2].kind = TransportKind::group;
  d.transports[2].collection = 0;
  d.transports[2].element = 1;
  d.transports[2].support = 0;
  d.finite_sets = {{0, 1, 2, 3, 4, 5}};
  d.transports[3].kind = TransportKind::join;
  d.transports[3].parts = {0, 2};
  d.actions = {{0, 0, true_ref, 7}};
  d.requests = {{{1}, 0, 1, true_ref}};
  return d;
}
void cube_centers() {
  using Matrix = std::array<int, 9>;
  const Matrix identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<Matrix> rotations{identity};
  std::array<int, 3> axes{0, 1, 2};
  do {
    int inversions = 0;
    for (int i = 0; i < 3; ++i)
      for (int j = i + 1; j < 3; ++j)
        inversions += axes[i] > axes[j];
    for (int bits = 0; bits < 8; ++bits) {
      Matrix m{};
      int determinant = inversions % 2 ? -1 : 1;
      for (int i = 0; i < 3; ++i) {
        int sign = (bits & (1 << i)) ? -1 : 1;
        determinant *= sign;
        m[3 * i + axes[i]] = sign;
      }
      if (determinant == 1 && m != identity)
        rotations.push_back(m);
    }
  } while (std::next_permutation(axes.begin(), axes.end()));
  check(rotations.size() == 24);
  Group g;
  g.order = 24;
  g.inverses.resize(24);
  for (Index a = 0; a < 24; ++a)
    for (Index b = 0; b < 24; ++b) {
      Matrix m{};
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
          for (int k = 0; k < 3; ++k)
            m[3 * i + j] += rotations[a][3 * i + k] * rotations[b][3 * k + j];
      auto id = static_cast<Index>(std::find(rotations.begin(), rotations.end(), m) - rotations.begin());
      g.products.push_back(id);
      if (id == 0)
        g.inverses[a] = b;
    }
  ProgramData d;
  d.groups = {g};
  d.piece_count = 1;
  d.placement_count = 30;
  d.initial = {0};
  d.finite_actions.push_back({0, 24, ActionKind::regular, {}});
  const std::array<std::array<int, 3>, 6> normals{
      {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
  FiniteAction a{0, 6, ActionKind::tabulated, {}};
  for (const auto &m : rotations)
    for (const auto &q : normals) {
      std::array<int, 3> v{};
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
          v[i] += m[3 * i + j] * q[j];
      a.table.push_back(static_cast<Index>(std::find(normals.begin(), normals.end(), v) - normals.begin()));
    }
  d.finite_actions.push_back(a);
  std::vector<Index> marked(24);
  std::iota(marked.begin(), marked.end(), 0);
  d.collections = {{0, marked}, {1, {24, 25, 26, 27, 28, 29}}};
  Program p(d);
  for (Index g = 0; g < 24; ++g)
    for (Index h = 0; h < 24; ++h)
      for (Index q = 0; q < 30; ++q) {
        const auto c = q < 24 ? 0 : 1;
        check(p.act(c, d.groups[0].products[g * 24 + h], q) == p.act(c, g, p.act(c, h, q)));
      }
}
} // namespace
int main() {
  try {
    cube_centers();
    auto d = fixture();
    Program p(d);
    for (Index g = 0; g < 6; ++g)
      for (Index h = 0; h < 6; ++h)
        for (Index q = 0; q < 10; ++q) {
          const Index c = q < 6 ? 0 : q < 9 ? 1 : 2;
          check(p.act(c, d.groups[0].products[g * 6 + h], q) == p.act(c, g, p.act(c, h, q)));
        }
    check(!p.transport(0, 6) && p.transport(0, 7) == 8 && p.transport(0, 8) == 6);
    for (Index q = 6; q < 9; ++q) {
      auto image = p.transport(0, q);
      auto actual = p.transport(1, p.act(1, 2, q));
      check(actual == (image ? std::optional<Index>{p.act(1, 2, *image)} : std::nullopt));
    }
    check(p.plan(0, State{7}, Choices{0}).transition->after == State{8});
    check(p.transport(3, 0) == p.transport(2, 0) && p.transport(3, 8) == 6);
    auto loop = d;
    loop.transports[0].target = 1;
    auto t = Program(loop).plan(0, State{7}, Choices{0});
    check(t.transition->actions.size() == 1 && t.transition->after == State{7});
    auto derived = d;
    derived.transports[0].source = 0;
    derived.transports[0].target = 3;
    Program derived_program(derived);
    for (Index q = 6; q < 9; ++q) {
      auto wanted = q == 6   ? std::optional<Index>{6 + derived.finite_actions[1].table[3 + 1]}
                    : q == 7 ? std::optional<Index>{6 + derived.finite_actions[1].table[3 + 2]}
                             : std::nullopt;
      check(derived_program.transport(0, q) == wanted);
    }
    auto input = d;
    input.permutations[3].input_action = 1;
    input.permutations[3].input_element = 2;
    input.transports[0].source = 0;
    input.transports[0].target = 3;
    Program input_program(input);
    for (Index q = 6; q < 8; ++q) {
      auto r = input.finite_actions[1].table[input.groups[0].inverses[2] * 3 + q - 6];
      auto wanted = r < 2 ? std::optional<Index>{6 + input.finite_actions[1].table[3 + r + 1]} : std::nullopt;
      check(input_program.transport(0, q) == wanted);
    }
    auto bad = d;
    bad.groups[0].products[1] = 2;
    rejects([&] { Program x(bad); });
    bad = d;
    bad.finite_actions[1].table[0] = 1;
    rejects([&] { Program x(bad); });
    bad = d;
    bad.collections[1].placements[0] = 0;
    rejects([&] { Program x(bad); });
    bad = d;
    bad.reference_bindings[0].placements[1] = 6;
    rejects([&] { Program x(bad); });
    bad = d;
    bad.permutations[3].base = 3;
    rejects([&] { Program x(bad); });
    bad = d;
    bad.transports[3].parts = {3};
    rejects([&] { Program x(bad); });
    bad = d;
    bad.transports[3].parts = {0, 1};
    rejects([&] { Program x(bad); });
    rejects([&] { p.act(0, 6, 0); });
    rejects([&] { p.act(0, 0, 7); });
    std::cout << "Finite actions and reference differences passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
