#include "compact.hpp"
#include <algorithm>
#include <set>

namespace twisty::language {
namespace {
using namespace core;
void fields(const Json &j, std::initializer_list<const char *> allowed) {
  if (!j.is_object())
    throw Error("language.invalid", "Expected compact object");
  for (const auto &[key, value] : j.items()) {
    (void)value;
    if (std::none_of(allowed.begin(), allowed.end(), [&](auto p) { return key == p; }))
      throw Error("language.invalid", "Unknown compact field: " + key);
  }
}
Index number(const Json &j) {
  if (!j.is_number_integer() ||
      (j.is_number_integer() && !j.is_number_unsigned() && j.get<std::int64_t>() < 0) ||
      j.get<std::uint64_t>() > 1000000)
    throw Error("language.invalid", "Expected bounded compact index");
  return j.get<Index>();
}
std::vector<Index> indices(const Json &j) {
  if (!j.is_array() || j.size() > 4000000)
    throw Error("language.invalid", "Expected compact index array");
  std::vector<Index> result;
  for (const auto &v : j)
    result.push_back(number(v));
  return result;
}
std::vector<Row> rows(const Json &j) {
  if (!j.is_array() || j.size() > 2000000)
    throw Error("language.invalid", "Expected bounded compact rows");
  std::vector<Row> out;
  for (const auto &r : j) {
    if (!r.is_array() || r.size() != 2)
      throw Error("language.invalid", "Expected source/target pair");
    out.push_back({number(r[0]), number(r[1])});
  }
  return out;
}
Json rows_json(const std::vector<Row> &v) {
  auto out = Json::array();
  for (const auto &r : v)
    out.push_back({r.from, r.to});
  return out;
}
} // namespace
void read_compact(const Json &j, core::ProgramData &d, int version) {
  using namespace core;
  const bool rectangular = version >= 3;
  if (rectangular)
    fields(j, {"groups", "actions", "collections", "spaces", "positionMaps", "bindings", "families", "maps"});
  else
    fields(j, {"groups", "actions", "collections", "spaces", "permutations", "bindings", "families", "maps"});
  const Json empty = Json::array();
  const auto array = [&](const char *key) -> const Json & {
    if (!j.contains(key))
      return empty;
    const auto &a = j.at(key);
    if (!a.is_array() || a.size() > 1000000)
      throw Error("language.invalid", "Expected compact declaration array");
    return a;
  };
  for (const auto &g : array("groups")) {
    fields(g, {"order", "products", "inverses"});
    d.groups.push_back({number(g.at("order")), indices(g.at("products")), indices(g.at("inverses"))});
  }
  for (const auto &a : array("actions")) {
    fields(a, {"group", "size", "kind", "table"});
    const auto kind = a.at("kind").get<std::string>();
    if (kind != "regular" && kind != "tabulated" && kind != "trivial")
      throw Error("language.invalid", "Unknown action kind");
    d.finite_actions.push_back({number(a.at("group")), number(a.at("size")),
                                kind == "regular"     ? ActionKind::regular
                                : kind == "tabulated" ? ActionKind::tabulated
                                                      : ActionKind::trivial,
                                indices(a.value("table", empty))});
  }
  for (const auto &c : array("collections")) {
    fields(c, {"id", "action", "placements"});
    d.collections.push_back({number(c.at("action")), indices(c.at("placements"))});
  }
  for (const auto &s : array("spaces")) {
    fields(s, {"size"});
    d.reference_spaces.push_back({number(s.at("size"))});
  }
  for (const auto &p : array(rectangular ? "positionMaps" : "permutations")) {
    if (rectangular) {
      if (p.contains("base"))
        fields(p, {"sourceSpace", "targetSpace", "base", "inputAction", "outputAction", "inputElement", "outputElement"});
      else fields(p, {"sourceSpace", "targetSpace", "rows"});
    } else {
      if (p.contains("base"))
        fields(p, {"space", "base", "inputAction", "outputAction", "inputElement", "outputElement"});
      else fields(p, {"space", "rows"});
    }
    if (!p.contains("base") && !p.contains("rows"))
      throw Error("language.invalid", "A position needs rows or a base");
    PositionMap v;
    v.source_space = number(p.at(rectangular ? "sourceSpace" : "space"));
    v.target_space = number(p.at(rectangular ? "targetSpace" : "space"));
    if (p.contains("rows")) v.rows = rows(p.at("rows"));
    if (p.contains("base")) v.base = number(p.at("base"));
    if (p.contains("inputAction")) v.input_action = number(p.at("inputAction"));
    if (p.contains("outputAction")) v.output_action = number(p.at("outputAction"));
    v.input_element = number(p.value("inputElement", Json(0)));
    v.output_element = number(p.value("outputElement", Json(0)));
    d.position_maps.push_back(std::move(v));
  }
  for (const auto &b : array("bindings")) {
    fields(b, {"space", "placements", "base", "group", "element"});
    if (b.contains("base"))
      fields(b, {"space", "base", "group", "element"});
    else
      fields(b, {"space", "placements"});
    ReferenceBinding v;
    v.space = number(b.at("space"));
    if (b.contains("placements"))
      v.placements = indices(b.at("placements"));
    if (b.contains("base"))
      v.base = number(b.at("base"));
    v.group = number(b.value("group", Json(0)));
    v.element = number(b.value("element", Json(0)));
    d.reference_bindings.push_back(std::move(v));
  }
  for (const auto &f : array("families")) {
    if (rectangular) fields(f, {"anchorSpace", "referenceSpace", "positions"});
    else fields(f, {"space", "positions"});
    d.twist_families.push_back({number(f.at(rectangular ? "anchorSpace" : "space")),
                              number(f.at(rectangular ? "referenceSpace" : "space")), indices(f.at("positions"))});
  }
  for (const auto &m : array("maps")) {
    fields(m, {"kind", "rows", "collection", "element", "support", "family", "binding", "source", "target",
               "parts"});
    const auto kind = m.at("kind").get<std::string>();
    Transport t;
    d.maps.emplace_back();
    if (kind == "explicit") {
      fields(m, {"kind", "rows"});
      d.maps.back() = rows(m.at("rows"));
    } else if (kind == "group") {
      fields(m, {"kind", "collection", "element", "support"});
      t.kind = TransportKind::group;
      t.collection = number(m.at("collection"));
      t.element = number(m.at("element"));
      auto support = indices(m.at("support"));
      if (!std::is_sorted(support.begin(), support.end()) ||
          std::adjacent_find(support.begin(), support.end()) != support.end())
        throw Error("language.invalid", "Group support must be sorted and unique");
      const auto it = std::find(d.finite_sets.begin(), d.finite_sets.end(), support);
      if (it == d.finite_sets.end()) {
        t.support = static_cast<Index>(d.finite_sets.size());
        d.finite_sets.push_back(std::move(support));
      } else
        t.support = static_cast<Index>(it - d.finite_sets.begin());
    } else if (kind == "difference") {
      fields(m, {"kind", "family", "binding", "source", "target"});
      t.kind = TransportKind::difference;
      t.family = number(m.at("family"));
      t.binding = number(m.at("binding"));
      t.source = number(m.at("source"));
      t.target = number(m.at("target"));
    } else if (kind == "union") {
      fields(m, {"kind", "parts"});
      t.kind = TransportKind::join;
      t.parts = indices(m.at("parts"));
    } else
      throw Error("language.invalid", "Unknown transport kind");
    d.transports.push_back(std::move(t));
  }
}
Json write_compact(const core::ProgramData &d, int version) {
  using namespace core;
  const bool rectangular = version >= 3;
  const auto positions_key = rectangular ? "positionMaps" : "permutations";
  Json j = Json::object();
  for (auto key :
       {"groups", "actions", "collections", "spaces", "permutations", "bindings", "families", "maps"})
    j[key] = Json::array();
  if (rectangular) { j.erase("permutations"); j[positions_key] = Json::array(); }
  for (const auto &g : d.groups)
    j["groups"].push_back({{"order", g.order}, {"products", g.products}, {"inverses", g.inverses}});
  for (const auto &a : d.finite_actions) {
    Json v{{"group", a.group},
           {"size", a.size},
           {"kind", a.kind == ActionKind::regular     ? "regular"
                    : a.kind == ActionKind::tabulated ? "tabulated"
                                                      : "trivial"}};
    if (!a.table.empty())
      v["table"] = a.table;
    j["actions"].push_back(std::move(v));
  }
  for (const auto &c : d.collections)
    j["collections"].push_back({{"action", c.action}, {"placements", c.placements}});
  for (const auto &s : d.reference_spaces)
    j["spaces"].push_back({{"size", s.size}});
  for (const auto &p : d.position_maps) {
    if (!rectangular && p.source_space != p.target_space)
      throw Error("language.invalid", "Rectangular position maps require version 3");
    Json v = rectangular ? Json{{"sourceSpace", p.source_space}, {"targetSpace", p.target_space}}
                         : Json{{"space", p.source_space}};
    if (p.base == no_index)
      v["rows"] = rows_json(p.rows);
    else {
      v["base"] = p.base;
      if (p.input_action != no_index) {
        v["inputAction"] = p.input_action;
        v["inputElement"] = p.input_element;
      }
      if (p.output_action != no_index) {
        v["outputAction"] = p.output_action;
        v["outputElement"] = p.output_element;
      }
    }
    j[positions_key].push_back(std::move(v));
  }
  for (const auto &b : d.reference_bindings) {
    Json v{{"space", b.space}};
    if (b.base == no_index)
      v["placements"] = b.placements;
    else {
      v["base"] = b.base;
      v["group"] = b.group;
      v["element"] = b.element;
    }
    j["bindings"].push_back(std::move(v));
  }
  for (const auto &f : d.twist_families) {
    if (!rectangular && f.anchor_space != f.reference_space)
      throw Error("language.invalid", "Distinct family spaces require version 3");
    j["families"].push_back(rectangular ? Json{{"anchorSpace", f.anchor_space}, {"referenceSpace", f.reference_space}, {"positions", f.positions}}
                                        : Json{{"space", f.reference_space}, {"positions", f.positions}});
  }
  for (Index i = 0; i < d.maps.size(); ++i) {
    const auto t = d.transports.empty() ? Transport{} : d.transports[i];
    Json v;
    switch (t.kind) {
    case TransportKind::explicit_map:
      v = {{"kind", "explicit"}, {"rows", rows_json(d.maps[i])}};
      break;
    case TransportKind::group:
      v = {{"kind", "group"},
           {"collection", t.collection},
           {"element", t.element},
           {"support", d.finite_sets[t.support]}};
      break;
    case TransportKind::difference:
      v = {{"kind", "difference"},
           {"family", t.family},
           {"binding", t.binding},
           {"source", t.source},
           {"target", t.target}};
      break;
    case TransportKind::join:
      v = {{"kind", "union"}, {"parts", t.parts}};
      break;
    }
    j["maps"].push_back(std::move(v));
  }
  return j;
}
} // namespace twisty::language
