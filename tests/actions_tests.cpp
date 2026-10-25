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
  d.position_maps = {{0, 0, {{0, 0}, {1, 1}}}, {0, 0, {{0, 1}, {1, 2}}}, {0, 0, {{0, 2}, {1, 0}}}};
  PositionMap derived;
  derived.source_space = derived.target_space = 0;
  derived.base = 1;
  derived.output_action = 1;
  derived.output_element = 1;
  d.position_maps.push_back(derived);
  d.reference_bindings = {{0, {6, 7, 8}}};
  ReferenceBinding b;
  b.space = 0;
  b.base = 0;
  b.group = 0;
  b.element = 2;
  d.reference_bindings.push_back(b);
  d.twist_families = {{0, 0, {0, 1, 2, 3}}};
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
void rectangular_positions() {
  {
    auto wide = fixture();
    wide.reference_spaces = {{2}, {5}};
    wide.position_maps = {{0, 1, {{0, 4}, {1, 3}}}, {0, 1, {{0, 2}, {1, 1}}}};
    wide.reference_bindings = {{1, {5, 6, 7, 8, 9}}};
    wide.twist_families = {{0, 1, {0, 1}}};
    wide.maps.resize(1);
    wide.transports.resize(1);
    wide.transports[0].source = 0;
    wide.transports[0].target = 1;
    Program p(wide);
    check(p.position_map(0, 0) == 4 && p.position_map(0, 4, true) == 0);
    check(p.transport(0, 9) == 7 && !p.transport(0, 5));
  }
  auto d = fixture();
  d.reference_spaces = {{5}, {3}};
  d.position_maps = {{0, 1, {{0, 0}, {1, 1}}},
                     {0, 1, {{0, 1}, {3, 0}, {4, 2}}},
                     {0, 1, {{0, 2}, {3, 0}, {4, 1}}}};
  d.reference_bindings.resize(1);
  d.reference_bindings[0].space = 1;
  d.twist_families = {{0, 1, {0, 1, 2}}};
  d.transports[1] = d.transports[0];
  d.transports[1].source = 2;
  d.transports[1].target = 1;
  Program p(d);
  // Anchors 3 and 4 are absent at nominal position 0. They still connect 1 to 2.
  for (Index q = 6; q < 9; ++q) {
    const auto to = p.transport(0, q);
    check(to && p.transport(1, *to) == q);
  }
  check(p.transport(0, 6) == 6 && p.transport(0, 7) == 8 && p.transport(0, 8) == 7);
  check(!p.position_map(0, 3) && p.position_map(1, 3) == 0);
  check(p.position_map(1, 0, true) == 3 && p.position_map(1, 2, true) == 4);
  auto loop = p.plan(0, State{6}, Choices{0});
  check(loop.transition && loop.transition->after == State{6} && loop.transition->actions.size() == 1 &&
        loop.transition->actions[0].event == 7);
  check(p.plan(0, State{9}, Choices{0}).transition->actions.empty());
  rejects([&] { p.position_map(0, 5); });
  rejects([&] { p.position_map(0, 3, true); });
  rejects([&] { p.position_map(99, 0); });
  {
    auto large = d;
    large.reference_spaces[0].size = 1000000;
    large.position_maps[1].rows.back().from = 999999;
    large.position_maps[2].rows.back().from = 999999;
    Program sparse(large);
    check(sparse.position_map(1, 999999) == 2 && sparse.position_map(1, 2, true) == 999999);
    check(sparse.transport_storage_bytes() == p.transport_storage_bytes());
    large.reference_spaces[0].size = 1000001;
    rejects([&] { Program invalid(large); });
  }

  // Independent C5 action on A and noncommuting S3 action on R.
  Group cyclic;
  cyclic.order = 5;
  for (Index g = 0; g < 5; ++g) {
    cyclic.inverses.push_back((5-g)%5);
    for (Index h = 0; h < 5; ++h) cyclic.products.push_back((g+h)%5);
  }
  d.groups.push_back(cyclic);
  d.finite_actions.push_back({1, 5, ActionKind::regular, {}});
  for (Index in = 0; in < 5; ++in) for (Index out = 0; out < 6; ++out) {
    auto derived = d;
    PositionMap map;
    map.source_space = 0; map.target_space = 1; map.base = 1;
    map.input_action = 3; map.input_element = in;
    map.output_action = 1; map.output_element = out;
    derived.position_maps.push_back(map);
    Program actual(derived);
    std::vector<Row> expanded;
    for (const auto &r : d.position_maps[1].rows)
      expanded.push_back({(r.from+in)%5, d.finite_actions[1].table[out*3+r.to]});
    for (Index a = 0; a < 5; ++a) {
      std::optional<Index> wanted;
      for (const auto &r : expanded) if (r.from == a) wanted = r.to;
      check(actual.position_map(3, a) == wanted);
    }
    for (Index r = 0; r < 3; ++r) {
      std::optional<Index> wanted;
      for (const auto &row : expanded) if (row.to == r) wanted = row.from;
      check(actual.position_map(3, r, true) == wanted);
    }
    check(actual.data().position_maps[3].rows.empty());
  }
  const auto malformed = [&](auto mutate) { auto bad=d; mutate(bad); rejects([&]{Program x(bad);}); };
  malformed([](auto &x){x.position_maps[1].rows[2].from=5;});
  malformed([](auto &x){x.position_maps[1].rows[2].to=3;});
  malformed([](auto &x){x.position_maps[1].rows[2].to=0;});
  malformed([](auto &x){x.position_maps[1].rows[2].from=3;});
  malformed([](auto &x){x.position_maps[1].target_space=99;});
  malformed([](auto &x){x.reference_spaces.push_back({3});x.position_maps[2].target_space=2;});
  malformed([](auto &x){x.reference_spaces.push_back({3});x.reference_bindings[0].space=2;});
  malformed([](auto &x){x.reference_bindings[0].space=0;x.reference_bindings[0].placements={0,1,2,3,4};});
  for (bool input : {false,true}) malformed([&](auto &x){
    PositionMap m; m.source_space=0;m.target_space=1;m.base=0;
    if(input)m.input_action=1;else m.output_action=3;
    x.position_maps.push_back(m);
  });
  malformed([](auto &x){
    PositionMap m;m.source_space=0;m.target_space=1;m.base=3;x.position_maps.push_back(m);
  });
  malformed([](auto &x){
    x.reference_spaces.push_back({5});
    PositionMap m;m.source_space=2;m.target_space=1;m.base=0;x.position_maps.push_back(m);
  });
  malformed([](auto &x){
    x.reference_spaces.push_back({3});
    PositionMap m;m.source_space=0;m.target_space=2;m.base=0;x.position_maps.push_back(m);
  });
  malformed([](auto &x){
    for(Index i=0;i<66;++i){PositionMap m;m.source_space=0;m.target_space=1;m.base=x.position_maps.size()-1;x.position_maps.push_back(m);}
  });
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
    rectangular_positions();
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
    input.position_maps[3].input_action = 1;
    input.position_maps[3].input_element = 2;
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
    bad.position_maps[3].base = 3;
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
