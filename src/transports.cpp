#include "twisty/core.hpp"
#include <algorithm>
#include <numeric>
#include <set>

namespace twisty::core {
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw Error("core.program", message);
}
std::optional<Index> lookup(const std::vector<Row> &rows, Index q) {
  const auto i =
      std::lower_bound(rows.begin(), rows.end(), q, [](const Row &r, Index x) { return r.from < x; });
  if (i == rows.end() || i->from != q)
    return {};
  return i->to;
}
std::vector<Row> reverse(const std::vector<Row> &rows, Index size) {
  std::vector<Row> out;
  for (const auto &r : rows) {
    check(r.from < size && r.to < size, "Unknown reference in partial permutation");
    check(out.empty() || rows[out.size() - 1].from < r.from, "Unsorted or duplicate reference source");
    out.push_back({r.to, r.from});
  }
  std::sort(out.begin(), out.end(), [](const Row &a, const Row &b) { return a.from < b.from; });
  for (std::size_t i = 1; i < out.size(); ++i)
    check(out[i - 1].from != out[i].from, "Partial map is not injective");
  return out;
}
} // namespace
Index Program::finite_action(Index a, Index g, Index q) const {
  const auto &action = data_.finite_actions[a];
  switch (action.kind) {
  case ActionKind::regular:
    return data_.groups[action.group].products[g * action.size + q];
  case ActionKind::tabulated:
    return action.table[g * action.size + q];
  case ActionKind::trivial:
    return q;
  }
  return no_index;
}
Index Program::act(Index c, Index g, Index q) const {
  if (c >= data_.collections.size() || q >= data_.placement_count || collection_of_[q] != c)
    throw Error("core.input", "Placement does not belong to the requested collection");
  const auto &collection = data_.collections[c];
  const auto &action = data_.finite_actions[collection.action];
  if (g >= data_.groups[action.group].order)
    throw Error("core.input", "Unknown group element");
  return collection.placements[finite_action(collection.action, g, local_of_[q])];
}
std::optional<Index> Program::permutation(Index id, Index q, bool inverse) const {
  const auto &p = data_.permutations[id];
  if (p.base == no_index)
    return lookup(inverse ? permutation_inverses_[id] : p.rows, q);
  const auto pre = inverse ? p.output_action : p.input_action;
  const auto post = inverse ? p.input_action : p.output_action;
  const auto pre_g = inverse ? p.output_element : p.input_element;
  const auto post_g = inverse ? p.input_element : p.output_element;
  if (pre != no_index)
    q = finite_action(pre, data_.groups[data_.finite_actions[pre].group].inverses[pre_g], q);
  auto result = permutation(p.base, q, inverse);
  if (result && post != no_index)
    result = finite_action(post, post_g, *result);
  return result;
}
Index Program::bind(Index id, Index q, bool inverse) const {
  const auto &b = data_.reference_bindings[id];
  if (b.base == no_index) {
    if (!inverse)
      return b.placements[q];
    return lookup(binding_inverses_[id], q).value_or(no_index);
  }
  if (inverse) {
    const auto c = collection_of_[q];
    if (data_.finite_actions[data_.collections[c].action].group != b.group)
      return no_index;
    return bind(b.base, act(c, data_.groups[b.group].inverses[b.element], q), true);
  }
  const auto base = bind(b.base, q, false);
  return act(collection_of_[base], b.element, base);
}
std::optional<Index> Program::transport_unchecked(Index id, Index q) const {
  if (data_.transports.empty())
    return lookup(data_.maps[id], q);
  const auto &t = data_.transports[id];
  switch (t.kind) {
  case TransportKind::explicit_map:
    return lookup(data_.maps[id], q);
  case TransportKind::group: {
    const auto &support = data_.finite_sets[t.support];
    if (!std::binary_search(support.begin(), support.end(), q))
      return {};
    return act(t.collection, t.element, q);
  }
  case TransportKind::difference: {
    const auto r = bind(t.binding, q, true);
    if (r == no_index)
      return {};
    const auto &f = data_.twist_families[t.family];
    const auto anchor = permutation(f.positions[t.source], r, true);
    if (!anchor)
      return {};
    const auto target = permutation(f.positions[t.target], *anchor, false);
    if (!target)
      return {};
    return bind(t.binding, *target, false);
  }
  case TransportKind::join:
    for (auto part : t.parts)
      if (const auto result = transport_unchecked(part, q))
        return result;
    return {};
  }
  return {};
}
std::optional<Index> Program::transport(Index id, Index q) const {
  if (id >= data_.maps.size() || q >= data_.placement_count)
    throw Error("core.input", "Unknown transport or placement");
  return transport_unchecked(id, q);
}
void Program::validate_transports() {
  const auto &d = data_;
  std::uint64_t work = 0, entries = 0;
  const auto charge = [&](std::uint64_t n) {
    entries += n;
    if (entries > 4000000)
      throw Error("core.limit", "Too many group/reference entries");
  };
  const auto step = [&](std::uint64_t n = 1) {
    work += n;
    if (work > 64000000)
      throw Error("core.limit", "Transport validation exceeds work limit");
  };
  check(d.groups.size() <= 4096 && d.finite_actions.size() <= 1000000 &&
            d.collections.size() <= d.placement_count && d.permutations.size() <= 1000000 &&
            d.reference_bindings.size() <= 1000000 && d.reference_spaces.size() <= 1000000 &&
            d.twist_families.size() <= 1000000,
        "Too many transport descriptors");
  for (const auto &g : d.groups) {
    check(g.order > 0 && g.order <= 4096 && g.products.size() == std::uint64_t(g.order) * g.order &&
              g.inverses.size() == g.order,
          "Invalid group dimensions");
    charge(g.products.size() + g.inverses.size());
    for (auto x : g.products)
      check(x < g.order, "Unknown group product");
    for (Index a = 0; a < g.order; ++a) {
      check(g.products[a] == a && g.products[a * g.order] == a && g.inverses[a] < g.order,
            "Invalid group identity or inverse");
      const auto b = g.inverses[a];
      check(g.products[a * g.order + b] == 0 && g.products[b * g.order + a] == 0, "Invalid group inverse");
      step(std::uint64_t(g.order) * g.order);
      for (Index b = 0; b < g.order; ++b)
        for (Index c = 0; c < g.order; ++c)
          check(g.products[g.products[a * g.order + b] * g.order + c] ==
                    g.products[a * g.order + g.products[b * g.order + c]],
                "Group is not associative");
    }
  }
  for (Index i = 0; i < d.finite_actions.size(); ++i) {
    const auto &a = d.finite_actions[i];
    check(a.group < d.groups.size() && a.size > 0 && a.size <= 1000000, "Invalid finite action");
    const auto n = d.groups[a.group].order;
    check(a.kind == ActionKind::regular || a.kind == ActionKind::tabulated || a.kind == ActionKind::trivial,
          "Unknown finite action kind");
    if (a.kind == ActionKind::regular)
      check(a.size == n && a.table.empty(), "Invalid regular action");
    else if (a.kind == ActionKind::trivial)
      check(a.table.empty(), "Trivial action has a table");
    else {
      check(a.table.size() == std::uint64_t(n) * a.size, "Wrong action table dimensions");
      charge(a.table.size());
      for (auto q : a.table)
        check(q < a.size, "Action target outside its set");
      for (Index q = 0; q < a.size; ++q) {
        check(finite_action(i, 0, q) == q, "Identity does not fix action points");
        step(std::uint64_t(n) * n);
        for (Index g = 0; g < n; ++g)
          for (Index h = 0; h < n; ++h)
            check(finite_action(i, d.groups[a.group].products[g * n + h], q) ==
                      finite_action(i, g, finite_action(i, h, q)),
                  "Finite action violates group law");
      }
    }
  }
  if (!d.collections.empty()) {
    collection_of_.assign(d.placement_count, no_index);
    local_of_.resize(d.placement_count);
    for (Index c = 0; c < d.collections.size(); ++c) {
      const auto &v = d.collections[c];
      check(v.action < d.finite_actions.size() && v.placements.size() == d.finite_actions[v.action].size,
            "Invalid placement collection");
      charge(v.placements.size());
      for (Index q = 0; q < v.placements.size(); ++q) {
        const auto p = v.placements[q];
        check(p < d.placement_count && collection_of_[p] == no_index,
              "Collections do not partition placements");
        collection_of_[p] = c;
        local_of_[p] = q;
      }
    }
    check(std::find(collection_of_.begin(), collection_of_.end(), no_index) == collection_of_.end(),
          "Placement is missing from collections");
  }
  for (const auto &s : d.reference_spaces)
    check(s.size > 0 && s.size <= 1000000, "Invalid reference space");
  permutation_inverses_.resize(d.permutations.size());
  std::vector<Index> depths(d.permutations.size());
  for (Index i = 0; i < d.permutations.size(); ++i) {
    const auto &p = d.permutations[i];
    check(p.space < d.reference_spaces.size(), "Unknown reference space");
    if (p.base == no_index) {
      check(p.input_action == no_index && p.output_action == no_index,
            "Explicit permutation has derived fields");
      charge(p.rows.size() * 2);
      permutation_inverses_[i] = reverse(p.rows, d.reference_spaces[p.space].size);
    } else {
      check(p.base < i && d.permutations[p.base].space == p.space && p.rows.empty(),
            "Cyclic, forward or incompatible permutation dependency");
      depths[i] = depths[p.base] + 1;
      check(depths[i] <= 64, "Permutation derivation is too deep");
      for (auto [action, element] :
           {std::pair{p.input_action, p.input_element}, std::pair{p.output_action, p.output_element}})
        if (action != no_index)
          check(action < d.finite_actions.size() &&
                    d.finite_actions[action].size == d.reference_spaces[p.space].size &&
                    element < d.groups[d.finite_actions[action].group].order,
                "Invalid reference action");
    }
  }
  binding_inverses_.resize(d.reference_bindings.size());
  depths.assign(d.reference_bindings.size(), 0);
  for (Index i = 0; i < d.reference_bindings.size(); ++i) {
    const auto &b = d.reference_bindings[i];
    check(b.space < d.reference_spaces.size(), "Unknown binding space");
    if (b.base == no_index) {
      check(b.placements.size() == d.reference_spaces[b.space].size, "Binding must cover its reference set");
      charge(b.placements.size() * 3);
      std::vector<Row> rows;
      for (Index r = 0; r < b.placements.size(); ++r) {
        check(b.placements[r] < d.placement_count, "Unknown bound placement");
        rows.push_back({r, b.placements[r]});
      }
      binding_inverses_[i] = reverse(rows, std::max<Index>(d.placement_count, b.placements.size()));
    } else {
      check(b.base < i && d.reference_bindings[b.base].space == b.space && b.placements.empty() &&
                b.group < d.groups.size() && b.element < d.groups[b.group].order && !d.collections.empty(),
            "Invalid derived binding");
      depths[i] = depths[b.base] + 1;
      check(depths[i] <= 64, "Binding derivation is too deep");
      for (Index r = 0; r < d.reference_spaces[b.space].size; ++r) {
        step();
        const auto q = bind(b.base, r, false);
        check(d.finite_actions[d.collections[collection_of_[q]].action].group == b.group,
              "Binding action group differs from collection group");
      }
    }
  }
  for (const auto &f : d.twist_families) {
    check(f.space < d.reference_spaces.size() && !f.positions.empty(), "Invalid twist family");
    charge(f.positions.size());
    for (auto p : f.positions)
      check(p < d.permutations.size() && d.permutations[p].space == f.space, "Incompatible position map");
  }
  if (d.transports.empty())
    return;
  check(d.transports.size() == d.maps.size(), "Wrong transport descriptor count");
  depths.assign(d.transports.size(), 0);
  std::vector<std::uint64_t> costs(d.transports.size(), 1);
  // Only unions need a scratch image. Other forms are injective by construction.
  for (Index i = 0; i < d.transports.size(); ++i) {
    const auto &t = d.transports[i];
    if (t.kind == TransportKind::explicit_map)
      continue;
    check(d.maps[i].empty(), "Derived transport also stores expanded rows");
    if (t.kind == TransportKind::group) {
      check(t.collection < d.collections.size() && t.support < d.finite_sets.size(),
            "Invalid group transport");
      const auto &c = d.collections[t.collection];
      check(t.element < d.groups[d.finite_actions[c.action].group].order, "Unknown transport group element");
      for (auto q : d.finite_sets[t.support])
        check(q < d.placement_count && collection_of_[q] == t.collection, "Group support outside collection");
    } else if (t.kind == TransportKind::difference) {
      check(t.family < d.twist_families.size() && t.binding < d.reference_bindings.size(),
            "Invalid reference transport");
      const auto &f = d.twist_families[t.family];
      check(d.reference_bindings[t.binding].space == f.space && t.source < f.positions.size() &&
                t.target < f.positions.size(),
            "Incompatible twist positions or binding");
    } else {
      check(t.kind == TransportKind::join, "Unknown transport kind");
      charge(t.parts.size());
      for (auto part : t.parts) {
        check(part < i, "Cyclic or forward transport dependency");
        depths[i] = std::max(depths[i], depths[part] + 1);
        costs[i] += costs[part];
        check(costs[i] <= 10000, "Transport lookup work exceeds limit");
      }
      check(depths[i] <= 64, "Transport derivation is too deep");
      std::vector<Index> targets(d.placement_count, no_index);
      std::vector<Index> candidates, pending = t.parts;
      while (!pending.empty()) {
        const auto part = pending.back();
        pending.pop_back();
        step();
        const auto &v = d.transports[part];
        if (v.kind == TransportKind::join)
          pending.insert(pending.end(), v.parts.begin(), v.parts.end());
        else if (v.kind == TransportKind::explicit_map) {
          step(d.maps[part].size());
          for (const auto &r : d.maps[part])
            candidates.push_back(r.from);
        } else if (v.kind == TransportKind::group) {
          const auto &support = d.finite_sets[v.support];
          step(support.size());
          candidates.insert(candidates.end(), support.begin(), support.end());
        } else {
          const auto n = d.reference_spaces[d.reference_bindings[v.binding].space].size;
          step(n);
          for (Index r = 0; r < n; ++r)
            candidates.push_back(bind(v.binding, r, false));
        }
        check(candidates.size() <= 4000000 && pending.size() <= 4000000,
              "Union validation scratch exceeds limit");
      }
      std::sort(candidates.begin(), candidates.end());
      candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
      for (Index q : candidates) {
        std::optional<Index> image;
        for (auto part : t.parts) {
          step(costs[part]);
          const auto r = transport_unchecked(part, q);
          if (r) {
            check(!image || *image == *r, "Union rows disagree");
            image = r;
          }
        }
        if (image) {
          check(targets[*image] == no_index, "Union is not injective");
          targets[*image] = q;
        }
      }
    }
  }
}
std::size_t Program::transport_storage_bytes() const {
  std::size_t n = collection_of_.size() + local_of_.size();
  for (const auto &v : data_.maps)
    n += 2 * v.size();
  for (const auto &g : data_.groups)
    n += g.products.size() + g.inverses.size() + 1;
  for (const auto &a : data_.finite_actions)
    n += a.table.size() + 3;
  for (const auto &c : data_.collections)
    n += c.placements.size() + 1;
  for (const auto &p : data_.permutations)
    n += p.rows.size() * 2 + 6;
  for (const auto &b : data_.reference_bindings)
    n += b.placements.size() + 4;
  for (const auto &f : data_.twist_families)
    n += f.positions.size() + 1;
  for (const auto &t : data_.transports)
    n += t.parts.size() + 8;
  for (const auto &v : permutation_inverses_)
    n += v.size() * 2;
  for (const auto &v : binding_inverses_)
    n += v.size() * 2;
  return n * sizeof(Index) + data_.reference_spaces.size() * sizeof(ReferenceSpace);
}
} // namespace twisty::core
