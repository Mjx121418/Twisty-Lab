#include "twisty/relational_compiler.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace twisty::relational {
namespace {
using namespace flat;
[[noreturn]] void fail(const std::string &message) {
  throw Error("language.invalid", message);
}
void expect(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}
void fields(const Json &j, std::initializer_list<const char *> allowed) {
  expect(j.is_object(), "Expected an object");
  for (auto it = j.begin(); it != j.end(); ++it)
    expect(std::any_of(allowed.begin(), allowed.end(), [&](auto key) { return it.key() == key; }),
           "Unknown field: " + it.key());
}
std::string name(const Json &j) {
  expect(j.is_string() && !j.get_ref<const std::string &>().empty(), "Expected a nonempty name");
  return j.get<std::string>();
}
Index natural(const Json &j) {
  expect(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
         "Expected a nonnegative integer");
  const auto value = j.get<std::uint64_t>();
  expect(value <= 1000000, "Integer exceeds the language limit");
  return static_cast<Index>(value);
}
std::vector<std::string> names(const Json &j) {
  expect(j.is_array() && j.size() <= 1000000, "Expected a finite array of names");
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const auto &x : j) {
    auto value = name(x);
    expect(seen.insert(value).second, "Duplicate name: " + value);
    out.push_back(std::move(value));
  }
  return out;
}
template <class T> Index find(const std::vector<T> &values, const T &value) {
  const auto it = std::find(values.begin(), values.end(), value);
  expect(it != values.end(), "Unknown name: " + value);
  return static_cast<Index>(it - values.begin());
}

class Lowering {
public:
  ProgramData data;
  Bindings bindings;
  std::map<std::string, Index> piece_ids, placement_ids, domain_ids;
  std::map<std::vector<Index>, Index> sets;
  std::map<std::vector<Index>, Ref> nodes;
  std::map<std::vector<std::pair<Index, Index>>, Index> maps;
  std::map<std::string, Json> definitions;
  std::set<std::string> expanding;
  std::size_t expanded_nodes = 0, table_entries = 0;

  Index finite_set(std::vector<Index> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    if (const auto it = sets.find(values); it != sets.end())
      return it->second;
    expect((table_entries += values.size()) <= 2000000, "Finite tables exceed the compiler limit");
    const auto id = static_cast<Index>(data.finite_sets.size());
    sets.emplace(values, id);
    data.finite_sets.push_back(std::move(values));
    return id;
  }
  Ref intern(std::vector<Index> key, Predicate node, const std::vector<Ref> &children = {}) {
    if (const auto it = nodes.find(key); it != nodes.end())
      return it->second;
    expect(data.predicates.size() < 100000 && data.child_references.size() + children.size() <= 1000000,
           "Predicate expansion exceeds the compiler limit");
    node.first_child = static_cast<Index>(data.child_references.size());
    node.child_count = static_cast<Index>(children.size());
    const auto ref = static_cast<Ref>(data.predicates.size() + 2);
    data.child_references.insert(data.child_references.end(), children.begin(), children.end());
    data.predicates.push_back(node);
    nodes.emplace(std::move(key), ref);
    return ref;
  }
  Ref in(Bank bank, Index input, std::vector<Index> values) {
    if (values.empty())
      return false_ref;
    Predicate node;
    node.bank = bank;
    node.input = input;
    node.set = finite_set(std::move(values));
    return intern({0, static_cast<Index>(bank), input, node.set}, node);
  }
  Ref range(Index lower, Index upper, std::vector<Ref> children) {
    if (lower > upper || lower > children.size())
      return false_ref;
    upper = std::min(upper, static_cast<Index>(children.size()));
    Index truths = 0;
    std::vector<Ref> remaining;
    for (auto ref : children) {
      if (ref == true_ref)
        ++truths;
      else if (ref != false_ref)
        remaining.push_back(ref);
    }
    if (truths > upper)
      return false_ref;
    lower = lower > truths ? lower - truths : 0;
    upper -= truths;
    if (lower > remaining.size())
      return false_ref;
    upper = std::min(upper, static_cast<Index>(remaining.size()));
    if (lower == 0 && upper == remaining.size())
      return true_ref;
    // Sorting shares commutative expressions, but NEVER deduplicates counted terms.
    std::sort(remaining.begin(), remaining.end());
    Predicate node;
    node.opcode = Opcode::range;
    node.lower = lower;
    node.upper = upper;
    std::vector<Index> key{1, lower, upper};
    key.insert(key.end(), remaining.begin(), remaining.end());
    return intern(std::move(key), node, remaining);
  }
  Ref all(std::vector<Ref> refs) {
    const auto n = static_cast<Index>(refs.size());
    return range(n, n, std::move(refs));
  }
  Ref any(std::vector<Ref> refs) {
    const auto n = static_cast<Index>(refs.size());
    return range(1, n, std::move(refs));
  }
  Ref negate(Ref ref) {
    return range(0, 0, {ref});
  }
  Ref implies(Ref a, Ref b) {
    return any({negate(a), b});
  }
  Index symbol(const std::map<std::string, Index> &ids, const Json &j) const {
    const auto key = name(j);
    const auto it = ids.find(key);
    expect(it != ids.end(), "Unknown symbol: " + key);
    return it->second;
  }
  Ref expression(const Json &j, const std::string &request, bool state_only = false, Index depth = 0) {
    expect(++expanded_nodes <= 1000000 && depth <= 128, "Expression expansion exceeds the compiler limit");
    if (j.is_boolean())
      return j.get<bool>() ? true_ref : false_ref;
    expect(j.is_object() && j.size() == 1, "A predicate must have exactly one operator");
    const auto op = j.begin().key();
    const auto &value = j.begin().value();
    const auto sub = [&](const Json &x) { return expression(x, request, state_only, depth + 1); };
    if (op == "at") {
      fields(value, {"piece", "placements"});
      std::vector<Index> set;
      for (const auto &q : names(value.at("placements")))
        set.push_back(symbol(placement_ids, q));
      return in(Bank::state, symbol(piece_ids, value.at("piece")), std::move(set));
    }
    if (op == "chosen") {
      expect(!state_only, "A state-only predicate cannot read movement choices");
      fields(value, {"domain", "moves"});
      const auto domain = symbol(domain_ids, value.at("domain"));
      std::vector<Index> set;
      for (const auto &move : names(value.at("moves")))
        set.push_back(find(bindings.choice_values[domain], move));
      return in(Bank::choice, domain, std::move(set));
    }
    if (op == "request") {
      expect(!state_only, "A state-only predicate cannot read the request");
      const auto ids = names(value);
      for (const auto &id : ids)
        expect(std::any_of(bindings.requests.begin(), bindings.requests.end(),
                           [&](const auto &r) { return r.id == id; }),
               "Unknown request: " + id);
      return std::find(ids.begin(), ids.end(), request) != ids.end() ? true_ref : false_ref;
    }
    if (op == "ref") {
      const auto id = name(value);
      expect(definitions.contains(id), "Unknown predicate: " + id);
      expect(expanding.insert(id).second, "Recursive predicate: " + id);
      const auto ref = sub(definitions.at(id));
      expanding.erase(id);
      return ref;
    }
    if (op == "not")
      return negate(sub(value));
    if (op == "all" || op == "any" || op == "implies" || op == "iff") {
      expect(value.is_array(), "Boolean operands must be an array");
      if (op == "implies" || op == "iff")
        expect(value.size() == 2, "Expected two operands");
      std::vector<Ref> refs;
      for (const auto &x : value)
        refs.push_back(sub(x));
      if (op == "all")
        return all(std::move(refs));
      if (op == "any")
        return any(std::move(refs));
      if (op == "implies")
        return implies(refs[0], refs[1]);
      return all({implies(refs[0], refs[1]), implies(refs[1], refs[0])});
    }
    if (op == "count") {
      fields(value, {"min", "max", "of"});
      expect(value.at("of").is_array(), "Count operands must be an array");
      std::vector<Ref> refs;
      for (const auto &x : value.at("of"))
        refs.push_back(sub(x));
      return range(natural(value.at("min")), natural(value.at("max")), std::move(refs));
    }
    if (op == "occupiedBy") {
      fields(value, {"placement", "pieces"});
      const auto q = symbol(placement_ids, value.at("placement"));
      std::vector<Ref> refs;
      for (const auto &p : names(value.at("pieces")))
        refs.push_back(in(Bank::state, symbol(piece_ids, p), {q}));
      return any(std::move(refs));
    }
    fail("Unknown predicate operator: " + op);
  }
};

CompiledPuzzle compile_checked(const Json &source) {
  fields(source, {"kind", "languageVersion", "id", "domains", "predicates", "relations", "requests",
                  "invariant", "goal"});
  expect(source.at("kind") == "relational-puzzle" && source.at("languageVersion").is_number_integer() &&
             source.at("languageVersion") == 1,
         "Expected relational-puzzle languageVersion 1");
  Lowering c;
  c.bindings.puzzle_id = name(source.at("id"));
  const auto &domains = source.at("domains");
  expect(domains.is_array() && !domains.empty() && domains.size() <= 128, "Expected 1..128 domains");
  struct DomainData {
    std::vector<Index> placements, pieces;
  };
  std::vector<DomainData> catalogs;
  for (const auto &domain : domains) {
    fields(domain, {"id", "placements", "pieces", "moves"});
    const auto id = name(domain.at("id"));
    expect(id.find('/') == std::string::npos && !c.domain_ids.contains(id),
           "Invalid or duplicate domain: " + id);
    c.domain_ids.emplace(id, static_cast<Index>(catalogs.size()));
    c.bindings.choice_slots.push_back(id);
    c.bindings.choice_values.push_back({"idle"});
    DomainData catalog;
    for (const auto &q : names(domain.at("placements"))) {
      const auto index = static_cast<Index>(c.bindings.placements.size());
      catalog.placements.push_back(index);
      c.bindings.placements.push_back(id + "/" + q);
      c.placement_ids.emplace(id + "/" + q, index);
    }
    expect(!catalog.placements.empty() && c.bindings.placements.size() <= 1000000,
           "Placement catalog must be nonempty and within limits");
    expect(domain.at("pieces").is_array(), "Expected a piece array");
    for (const auto &p : domain.at("pieces")) {
      fields(p, {"id", "initial"});
      const auto piece = name(p.at("id"));
      expect(!c.piece_ids.contains(piece), "Duplicate piece: " + piece);
      const auto index = static_cast<Index>(c.bindings.pieces.size());
      c.piece_ids.emplace(piece, index);
      c.bindings.pieces.push_back(piece);
      catalog.pieces.push_back(index);
      const auto initial = c.symbol(c.placement_ids, id + "/" + name(p.at("initial")));
      c.data.initial.push_back(initial);
    }
    expect(c.bindings.pieces.size() <= 4096, "Too many pieces");
    expect(domain.at("moves").is_array() && domain.at("moves").size() <= 4096,
           "Expected a finite move array");
    for (const auto &move : domain.at("moves")) {
      const auto move_id = name(move.at("id"));
      auto &values = c.bindings.choice_values.back();
      expect(std::find(values.begin(), values.end(), move_id) == values.end(),
             "Duplicate movement: " + move_id);
      values.push_back(move_id);
    }
    catalogs.push_back(std::move(catalog));
  }
  c.data.piece_count = static_cast<Index>(c.bindings.pieces.size());
  c.data.placement_count = static_cast<Index>(c.bindings.placements.size());
  if (source.contains("predicates")) {
    expect(source.at("predicates").is_object(), "Expected named predicates");
    for (auto it = source.at("predicates").begin(); it != source.at("predicates").end(); ++it) {
      expect(!it.key().empty(), "Empty predicate name");
      c.definitions.emplace(it.key(), it.value());
    }
  }
  const auto &requests = source.at("requests");
  expect(requests.is_array() && requests.size() <= 4096, "Expected a finite request array");
  for (const auto &request : requests) {
    fields(request, {"id", "relation", "profiles", "defaultProfile"});
    const auto id = name(request.at("id"));
    expect(std::none_of(c.bindings.requests.begin(), c.bindings.requests.end(),
                        [&](const auto &r) { return r.id == id; }),
           "Duplicate request: " + id);
    c.bindings.requests.push_back({id, {}, {}});
  }
  std::vector<Ref> invariants;
  for (const auto &catalog : catalogs)
    for (auto piece : catalog.pieces)
      invariants.push_back(c.in(Bank::state, piece, catalog.placements));
  invariants.push_back(c.expression(source.value("invariant", Json(true)), "", true));
  c.data.invariant = c.all(std::move(invariants));
  if (source.contains("goal"))
    c.data.goal = c.expression(source.at("goal"), "", true);
  else {
    std::vector<Ref> homes;
    for (Index p = 0; p < c.data.piece_count; ++p)
      homes.push_back(c.in(Bank::state, p, {c.data.initial[p]}));
    c.data.goal = c.all(std::move(homes));
  }
  // One shared action range. Every tuple has exactly one local choice per domain.
  struct Requirement {
    Ref selected, condition;
  };
  std::vector<Requirement> requirements;
  for (Index d = 0; d < domains.size(); ++d) {
    const auto domain_name = c.bindings.choice_slots[d];
    Index choice = 1;
    for (const auto &move : domains[d].at("moves")) {
      fields(move, {"id", "support", "transport", "pieces", "requires", "event"});
      const auto support = names(move.at("support"));
      const auto &table = move.at("transport");
      expect(table.is_object() && table.size() == support.size(),
             "Transport must be total on its declared support");
      std::vector<std::pair<Index, Index>> key;
      std::set<Index> targets;
      for (const auto &q : support) {
        expect(table.contains(q), "Missing transport row: " + q);
        const auto from = c.symbol(c.placement_ids, domain_name + "/" + q);
        const auto to = c.symbol(c.placement_ids, domain_name + "/" + name(table.at(q)));
        expect(targets.insert(to).second, "Transport is not one-to-one");
        key.emplace_back(from, to);
      }
      std::sort(key.begin(), key.end());
      Index map;
      if (const auto it = c.maps.find(key); it != c.maps.end())
        map = it->second;
      else {
        expect((c.table_entries += key.size()) <= 2000000, "Finite tables exceed the compiler limit");
        map = static_cast<Index>(c.data.maps.size());
        c.maps.emplace(key, map);
        std::vector<Row> rows;
        for (auto [from, to] : key)
          rows.push_back({from, to});
        c.data.maps.push_back(std::move(rows));
      }
      auto pieces = catalogs[d].pieces;
      if (move.contains("pieces")) {
        pieces.clear();
        for (const auto &p : names(move.at("pieces"))) {
          const auto index = c.symbol(c.piece_ids, p);
          expect(std::find(catalogs[d].pieces.begin(), catalogs[d].pieces.end(), index) !=
                     catalogs[d].pieces.end(),
                 "A local move can only reference its domain's pieces");
          pieces.push_back(index);
        }
      }
      const auto selected = c.in(Bank::choice, d, {choice++});
      requirements.push_back({selected, c.expression(move.value("requires", Json(true)), "", true)});
      const auto event_name = name(move.value("event", Json(domain_name + "/" + name(move.at("id")))));
      auto event = std::find(c.bindings.events.begin(), c.bindings.events.end(), event_name);
      if (event == c.bindings.events.end())
        c.bindings.events.push_back(event_name);
      const auto event_id = find(c.bindings.events, event_name);
      for (auto piece : pieces)
        c.data.actions.push_back({piece, map, selected, event_id});
      expect(c.data.actions.size() <= 1000000, "Action expansion exceeds the compiler limit");
    }
  }
  std::set<std::string> relation_ids;
  const auto relations = source.value("relations", Json::array());
  expect(relations.is_array(), "Expected a relation array");
  for (const auto &relation : relations) {
    fields(relation, {"id", "predicate"});
    expect(relation_ids.insert(name(relation.at("id"))).second, "Duplicate relation ID");
  }
  for (Index r = 0; r < requests.size(); ++r) {
    const auto &source_request = requests[r];
    auto &binding = c.bindings.requests[r];
    Request request;
    for (const auto &values : c.bindings.choice_values)
      request.choice_sizes.push_back(static_cast<Index>(values.size()));
    request.action_count = static_cast<Index>(c.data.actions.size());
    std::vector<Ref> clauses{c.expression(source_request.at("relation"), binding.id)};
    for (const auto &relation : relations)
      clauses.push_back(c.expression(relation.at("predicate"), binding.id));
    for (const auto &requirement : requirements)
      clauses.push_back(c.implies(requirement.selected, requirement.condition));
    request.relation = c.all(std::move(clauses));
    c.data.requests.push_back(std::move(request));
    const auto profiles = source_request.value("profiles", Json::array());
    expect(profiles.is_array() && profiles.size() <= 4096, "Expected a finite profile array");
    for (const auto &profile : profiles) {
      fields(profile, {"id", "choices", "when"});
      Profile p;
      p.id = name(profile.at("id"));
      expect(std::none_of(binding.profiles.begin(), binding.profiles.end(),
                          [&](const auto &x) { return x.id == p.id; }),
             "Duplicate profile: " + p.id);
      const auto &tuple = profile.at("choices");
      expect(tuple.is_object() && tuple.size() == catalogs.size(), "Profile must supply every local choice");
      for (Index d = 0; d < catalogs.size(); ++d)
        p.choices.push_back(find(c.bindings.choice_values[d], name(tuple.at(c.bindings.choice_slots[d]))));
      p.when = c.expression(profile.value("when", Json(true)), "", true);
      binding.profiles.push_back(std::move(p));
    }
    if (source_request.contains("defaultProfile")) {
      const auto id = name(source_request.at("defaultProfile"));
      const auto it = std::find_if(binding.profiles.begin(), binding.profiles.end(),
                                   [&](const auto &p) { return p.id == id; });
      expect(it != binding.profiles.end(), "Unknown default profile: " + id);
      binding.default_profile = static_cast<Index>(it - binding.profiles.begin());
    }
  }
  // Even unused named predicates must be well-typed and nonrecursive.
  for (const auto &[id, expr] : c.definitions) {
    (void)expr;
    c.expression(Json{{"ref", id}}, c.bindings.requests.empty() ? "" : c.bindings.requests.front().id);
  }
  return {Program(std::move(c.data)), std::move(c.bindings)};
}
} // namespace

CompiledPuzzle compile(const Json &source) {
  try {
    return compile_checked(source);
  } catch (const Json::exception &error) {
    throw flat::Error("language.invalid", error.what());
  }
}
CompiledPuzzle compile_text(const std::string &source) {
  if (source.size() > 8 * 1024 * 1024)
    throw flat::Error("language.limit", "Source exceeds 8 MiB");
  // Bound nesting before constructing the DOM, including unreferenced source data.
  try {
    auto parsed = Json::parse(source, [](int depth, Json::parse_event_t, Json &) {
      if (depth > 256)
        throw flat::Error("language.limit", "Source nesting exceeds 256");
      return true;
    });
    return compile(parsed);
  } catch (const Json::exception &error) {
    throw flat::Error("language.invalid", error.what());
  }
}
} // namespace twisty::relational
