#include "authoring/compact.hpp"
#include "json_input.hpp"
#include "twisty/completion.hpp"
#include "twisty/authoring/compiler.hpp"
#include <algorithm>

namespace twisty::language {
namespace {
Json error_json(const std::string &code, const std::string &message) {
  return {{"status", "Invalid"}, {"diagnostics", Json::array({{{"code", code}, {"message", message}}})}};
}
template <class F> std::string report(F action) {
  try {
    return action().dump();
  } catch (const core::Error &error) {
    return error_json(error.code, error.what()).dump();
  } catch (const Json::exception &error) {
    return error_json("language.invalid", error.what()).dump();
  }
}
core::Index index_of(const std::vector<std::string> &values, const std::string &name) {
  const auto it = std::find(values.begin(), values.end(), name);
  if (it == values.end())
    throw core::Error("core.input", "Unknown name: " + name);
  return static_cast<core::Index>(it - values.begin());
}
core::State numbers(const Json &value) {
  if (!value.is_array() || value.size() > 4096)
    throw core::Error("core.input", "Expected a finite input array");
  core::State result;
  for (const auto &x : value) {
    if (!x.is_number_integer() || (!x.is_number_unsigned() && x.get<std::int64_t>() < 0) ||
        x.get<std::uint64_t>() > 1000000)
      throw core::Error("core.input", "Expected finite nonnegative integer inputs");
    result.push_back(x.get<core::Index>());
  }
  return result;
}
Json named_choices(const Bindings &bindings, const core::Choices &choices) {
  auto result = Json::array();
  for (std::size_t i = 0; i < choices.size(); ++i)
    result.push_back(bindings.choice_values[i][choices[i]]);
  return result;
}
core::Choices read_choices(const Bindings &bindings, const Json &value) {
  if (!value.is_array() || value.size() != bindings.choice_slots.size())
    throw core::Error("core.choice", "Supply one movement name per choice slot, including idle");
  core::Choices result;
  for (std::size_t i = 0; i < value.size(); ++i)
    result.push_back(index_of(bindings.choice_values[i], value[i].get<std::string>()));
  return result;
}
Json query(const CompiledPuzzle &p, const Json &q) {
  if (!q.is_object())
    throw core::Error("core.input", "Expected a query object");
  for (auto it = q.begin(); it != q.end(); ++it) {
    const std::vector<std::string> allowed{
        "op",         "state",   "request",   "choices", "profile", "candidateLimit", "resultLimit",
        "collection", "element", "placement", "map",     "point",   "inverse"};
    if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
      throw core::Error("core.input", "Unknown query field: " + it.key());
  }
  const auto op = q.at("op").get<std::string>();
  if (op == "act" || op == "transport" || op == "positionMap") {
    const auto value = [&](const char *key) { return numbers(Json::array({q.at(key)})).at(0); };
    if (op == "positionMap") {
      const auto target = p.program.position_map(value("map"), value("point"), q.value("inverse", false));
      return {{"status", "Evaluated"},
              {"point", target ? Json(*target) : Json(nullptr)},
              {"defined", target.has_value()}};
    }
    if (op == "act")
      return {{"status", "Evaluated"},
              {"placement", p.program.act(value("collection"), value("element"), value("placement"))}};
    const auto target = p.program.transport(value("map"), value("placement"));
    return {{"status", "Evaluated"},
            {"placement", target ? Json(*target) : Json(nullptr)},
            {"participates", target.has_value()}};
  }
  const auto state = numbers(q.at("state"));
  if (op == "validate")
    return {{"status", p.program.valid(state) ? "Valid" : "Invalid"}};
  if (op == "goal")
    return {{"status", "Evaluated"}, {"solved", p.program.solved(state)}};
  const auto request_name = q.at("request").get<std::string>();
  const auto request_it = std::find_if(p.bindings.requests.begin(), p.bindings.requests.end(),
                                       [&](const auto &r) { return r.id == request_name; });
  if (request_it == p.bindings.requests.end())
    throw core::Error("core.request", "Unknown request: " + request_name);
  const auto request = static_cast<core::Index>(request_it - p.bindings.requests.begin());
  if (op == "bind") {
    std::optional<std::string> profile;
    if (q.contains("profile"))
      profile = q.at("profile").get<std::string>();
    const auto choices = bind_profile(p, request, state, profile);
    if (!choices)
      return {{"status", "ChoiceRequired"}};
    return {{"status", "Selected"}, {"choices", named_choices(p.bindings, *choices)}};
  }
  if (op == "discover") {
    const auto candidate = numbers(Json::array({q.at("candidateLimit")}));
    const auto limit = numbers(Json::array({q.at("resultLimit")}));
    const auto result = discover(p.program, request, state, candidate[0], limit[0]);
    auto choices = Json::array();
    for (const auto &tuple : result.choices)
      choices.push_back(named_choices(p.bindings, tuple));
    return {{"status", "Discovered"},
            {"examined", result.examined},
            {"complete", result.complete},
            {"choices", choices}};
  }
  if (op != "plan")
    throw core::Error("core.input", "Unknown query operation: " + op);
  const auto result = p.program.plan(request, state, read_choices(p.bindings, q.at("choices")));
  if (!result.transition) {
    const auto reason = result.rejection == core::Rejection::relation           ? "relation"
                        : result.rejection == core::Rejection::target_invariant ? "target-invariant"
                                                                                : "conflicting-writes";
    return {{"status", "Rejected"}, {"reason", reason}};
  }
  const auto &t = *result.transition;
  auto actions = Json::array();
  for (const auto &a : t.actions)
    actions.push_back({{"piece", a.piece}, {"from", a.from}, {"to", a.to}, {"event", a.event}});
  return {{"status", "Permitted"},
          {"transition",
           {{"request", request_name},
            {"choices", named_choices(p.bindings, t.choices)},
            {"before", t.before},
            {"after", t.after},
            {"actions", actions}}}};
}
} // namespace

Json inspect(const CompiledPuzzle &p) {
  const auto &d = p.program.data();
  auto predicates = Json::array(), maps = Json::array(), actions = Json::array(), requests = Json::array();
  for (const auto &node : d.predicates) {
    if (node.opcode == core::Opcode::in)
      predicates.push_back({{"op", "IN"},
                            {"bank", node.bank == core::Bank::state ? "state" : "choice"},
                            {"input", node.input},
                            {"set", node.set}});
    else
      predicates.push_back({{"op", "RANGE"},
                            {"min", node.lower},
                            {"max", node.upper},
                            {"firstChild", node.first_child},
                            {"childCount", node.child_count}});
  }
  for (const auto &map : d.maps) {
    auto rows = Json::array();
    for (const auto &row : map)
      rows.push_back(Json::array({row.from, row.to}));
    maps.push_back(std::move(rows));
  }
  for (const auto &a : d.actions)
    actions.push_back(
        {{"piece", a.piece}, {"map", a.map}, {"selectionRoot", a.selection}, {"event", a.event}});
  auto bindings = Json{{"puzzleId", p.bindings.puzzle_id},       {"pieces", p.bindings.pieces},
                       {"placements", p.bindings.placements},    {"events", p.bindings.events},
                       {"choiceSlots", p.bindings.choice_slots}, {"choiceValues", p.bindings.choice_values}};
  auto request_bindings = Json::array();
  for (std::size_t i = 0; i < d.requests.size(); ++i) {
    const auto &r = d.requests[i];
    requests.push_back({{"choiceSizes", r.choice_sizes},
                        {"firstAction", r.first_action},
                        {"actionCount", r.action_count},
                        {"relationRoot", r.relation}});
    const auto &b = p.bindings.requests[i];
    auto profiles = Json::array();
    for (const auto &profile : b.profiles)
      profiles.push_back({{"id", profile.id},
                          {"choices", named_choices(p.bindings, profile.choices)},
                          {"whenRoot", profile.when}});
    request_bindings.push_back(
        {{"id", b.id},
         {"profiles", profiles},
         {"defaultProfile", b.default_profile ? Json(b.profiles[*b.default_profile].id) : Json(nullptr)}});
  }
  bindings["requests"] = std::move(request_bindings);
  Json result = {{"kind", "relational-preview"},
                 {"experimentalVersion", 1},
                 {"predicateVersion", 1},
                 {"program",
                  {{"pieceCount", d.piece_count},
                   {"placementCount", d.placement_count},
                   {"initialPlacements", d.initial},
                   {"finiteSets", d.finite_sets},
                   {"childReferences", d.child_references},
                   {"invariantRoot", d.invariant},
                   {"goalRoot", d.goal}}}};
  // Move the large export arrays into their final owners. Initializer lists
  // otherwise retain multiple copies of every action and predicate at once.
  result["bindings"] = std::move(bindings);
  auto &program = result["program"];
  program["predicateNodes"] = std::move(predicates);
  program["transportMaps"] = std::move(maps);
  program["actionRecords"] = std::move(actions);
  program["requestPrograms"] = std::move(requests);
  if (p.source_version >= 2) {
    result["experimentalVersion"] = p.source_version;
    result["program"].erase("transportMaps");
    result["program"]["compact"] = write_compact(d, p.source_version);
    result["program"]["transportStorageBytes"] = p.program.transport_storage_bytes();
    result["bindings"]["moveMaps"] = p.bindings.move_maps;
    result["bindings"]["collections"] = p.bindings.collection_names;
  }
  return result;
}
Puzzle::Puzzle(const std::string &source) : puzzle_(compile_text(source)) {}
std::string Puzzle::inspect_json() const {
  return inspect(puzzle_).dump();
}
std::string Puzzle::query_json(const std::string &text) const {
  return report([&]() {
    if (text.size() > 1024 * 1024)
      throw core::Error("core.limit", "Query exceeds 1 MiB");
    detail::check_json_depth(text, 64, [] { throw core::Error("core.limit", "Query nesting exceeds 64"); });
    return query(puzzle_, Json::parse(text));
  });
}
std::string compile_json(const std::string &source) {
  return report([&]() { return Json{{"status", "Compiled"}, {"preview", inspect(compile_text(source))}}; });
}
} // namespace twisty::language
