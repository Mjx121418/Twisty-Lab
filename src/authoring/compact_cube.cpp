#include "compact.hpp"
#include "twisty/symmetry.hpp"
#include <map>
#include <numeric>
#include <set>

namespace twisty::language {
void compact_cube_source(Json &source, const Json &catalog) {
  using core::Index;
  const std::string faces = "URFDLB";
  const auto group = catalog.at("symmetry").get<std::vector<symmetry::Permutation>>();
  std::vector<Index> products, inverses(group.size());
  for (Index g = 0; g < group.size(); ++g)
    for (Index h = 0; h < group.size(); ++h) {
      auto p = symmetry::compose(group[g], group[h]);
      auto i = static_cast<Index>(std::find(group.begin(), group.end(), p) - group.begin());
      products.push_back(i);
      if (i == 0)
        inverses[g] = h;
    }
  const auto act = [&](std::string key, Index g) {
    for (auto &ch : key) {
      const auto i = faces.find(ch);
      if (i != std::string::npos)
        ch = faces[group[g][i]];
    }
    return key;
  };
  Json c;
  for (auto key :
       {"groups", "actions", "collections", "spaces", "permutations", "bindings", "families", "maps"})
    c[key] = Json::array();
  c["groups"].push_back({{"order", group.size()}, {"products", products}, {"inverses", inverses}});
  std::map<std::string, Index> interned[8];
  const auto intern = [&](const char *field, int slot, Json value) {
    auto key = value.dump();
    auto &table = interned[slot];
    auto [i, added] = table.emplace(key, static_cast<Index>(c[field].size()));
    if (added)
      c[field].push_back(std::move(value));
    return i->second;
  };
  std::vector<std::vector<Index>> world(group.size());
  std::vector<Index> collection_of;
  Index offset = 0;
  for (const auto &d : source.at("domains")) {
    const auto keys = d.at("placements").get<std::vector<std::string>>();
    std::map<std::string, Index> ids;
    for (Index i = 0; i < keys.size(); ++i)
      ids[keys[i]] = offset + i;
    for (Index g = 0; g < group.size(); ++g)
      for (const auto &q : keys)
        world[g].push_back(ids.at(act(q, g)));
    std::set<Index> unseen;
    for (Index q = 0; q < keys.size(); ++q)
      unseen.insert(offset + q);
    collection_of.resize(offset + keys.size());
    while (!unseen.empty()) {
      const auto seed = *unseen.begin();
      std::vector<Index> orbit;
      for (Index g = 0; g < group.size(); ++g)
        if (std::find(orbit.begin(), orbit.end(), world[g][seed]) == orbit.end())
          orbit.push_back(world[g][seed]);
      Json a{{"group", 0},
             {"size", orbit.size()},
             {"kind", orbit.size() == group.size() ? "regular" : "tabulated"}};
      if (orbit.size() != group.size()) {
        std::vector<Index> table;
        for (Index g = 0; g < group.size(); ++g)
          for (auto q : orbit)
            table.push_back(
                static_cast<Index>(std::find(orbit.begin(), orbit.end(), world[g][q]) - orbit.begin()));
        a["table"] = table;
      }
      const auto action = intern("actions", 0, std::move(a));
      auto col = static_cast<Index>(c["collections"].size());
      c["collections"].push_back({{"action", action}, {"placements", orbit}});
      for (auto q : orbit) {
        unseen.erase(q);
        collection_of[q] = col;
      }
    }
    offset += static_cast<Index>(keys.size());
  }
  // The six cog phases form a standalone regular C6 reference action.
  auto cog_family = core::no_index;
  Index cog_space = 0;
  const auto phase_family = [&]() {
    if (cog_family != core::no_index)
      return cog_family;
    std::vector<Index> mul, inv;
    for (Index a = 0; a < 6; ++a) {
      inv.push_back((6 - a) % 6);
      for (Index b = 0; b < 6; ++b)
        mul.push_back((a + b) % 6);
    }
    auto g = static_cast<Index>(c["groups"].size());
    c["groups"].push_back({{"order", 6}, {"products", mul}, {"inverses", inv}});
    auto a = intern("actions", 0, {{"group", g}, {"size", 6}, {"kind", "regular"}});
    cog_space = static_cast<Index>(c["spaces"].size());
    c["spaces"].push_back({{"size", 6}});
    Json rows = Json::array();
    for (Index q = 0; q < 6; ++q)
      rows.push_back({q, q});
    auto p = static_cast<Index>(c["permutations"].size());
    c["permutations"].push_back({{"space", cog_space}, {"rows", rows}});
    c["permutations"].push_back(
        {{"space", cog_space}, {"base", p}, {"outputAction", a}, {"outputElement", 1}});
    c["permutations"].push_back(
        {{"space", cog_space}, {"base", p}, {"outputAction", a}, {"outputElement", 5}});
    cog_family = static_cast<Index>(c["families"].size());
    c["families"].push_back({{"space", cog_space}, {"positions", {p, p + 1, p + 2}}});
    return cog_family;
  };
  offset = 0;
  for (auto &d : source["domains"]) {
    auto keys = d.at("placements").get<std::vector<std::string>>();
    std::map<std::string, Index> ids;
    for (Index q = 0; q < keys.size(); ++q)
      ids[keys[q]] = offset + q;
    for (auto &m : d["moves"]) {
      std::map<Index, Index> remaining;
      for (const auto &[a, b] : m.at("transport").items())
        remaining[ids.at(a)] = ids.at(b.get<std::string>());
      std::vector<Index> parts;
      if (d.at("id") == "gear-edge") {
        std::vector<Index> sources;
        for (const auto &[q, to] : remaining) {
          (void)to;
          sources.push_back(q);
        }
        for (auto q : sources) {
          if (!remaining.contains(q))
            continue;
          const auto &from = keys[q - offset];
          auto pos = from.rfind("/p");
          if (pos == std::string::npos)
            continue;
          const auto base = from.substr(0, pos + 2);
          auto phase = static_cast<Index>(std::stoul(from.substr(pos + 2)));
          const auto to = remaining[q];
          const auto &target = keys[to - offset];
          if (target.substr(0, pos + 2) != base)
            continue;
          auto target_phase = static_cast<Index>(std::stoul(target.substr(pos + 2)));
          auto delta = (target_phase + 6 - phase) % 6;
          if (delta != 1 && delta != 5)
            continue;
          std::vector<Index> binding;
          bool complete = true;
          for (Index r = 0; r < 6; ++r) {
            auto a = ids.at(base + std::to_string(r)), b = ids.at(base + std::to_string((r + delta) % 6));
            binding.push_back(a);
            if (!remaining.contains(a) || remaining[a] != b)
              complete = false;
          }
          if (!complete)
            continue;
          const auto family = phase_family();
          auto b = intern("bindings", 1, {{"space", cog_space}, {"placements", binding}});
          parts.push_back(intern("maps", 2,
                                 {{"kind", "difference"},
                                  {"family", family},
                                  {"binding", b},
                                  {"source", 0},
                                  {"target", delta == 1 ? 1 : 2}}));
          for (auto r : binding)
            remaining.erase(r);
        }
      }
      std::map<Index, std::vector<Index>> by_collection;
      for (const auto &[q, to] : remaining) {
        (void)to;
        by_collection[collection_of[q]].push_back(q);
      }
      for (const auto &[col, qs] : by_collection) {
        // Group each compatible support; coupled opposite faces may use different elements.
        std::set<Index> pending(qs.begin(), qs.end());
        while (!pending.empty()) {
          Index best = 0;
          std::vector<Index> support;
          for (Index g = 0; g < group.size(); ++g) {
            std::vector<Index> candidate;
            for (auto q : pending)
              if (world[g][q] == remaining[q])
                candidate.push_back(q);
            if (candidate.size() > support.size()) {
              best = g;
              support = std::move(candidate);
            }
          }
          if (support.empty())
            break;
          parts.push_back(intern(
              "maps", 2, {{"kind", "group"}, {"collection", col}, {"element", best}, {"support", support}}));
          for (auto q : support) {
            pending.erase(q);
            remaining.erase(q);
          }
        }
      }
      if (!remaining.empty() || parts.empty()) {
        Json rows = Json::array();
        for (const auto &[q, to] : remaining)
          rows.push_back({q, to});
        parts.push_back(intern("maps", 2, {{"kind", "explicit"}, {"rows", rows}}));
      }
      const auto map =
          parts.size() == 1 ? parts[0] : intern("maps", 2, {{"kind", "union"}, {"parts", parts}});
      m.erase("transport");
      m.erase("support");
      m["map"] = map;
    }
    offset += static_cast<Index>(keys.size());
  }
  source["languageVersion"] = 2;
  source["compact"] = std::move(c);
}
} // namespace twisty::language
