#include "artifact_internal.hpp"
#include "compact.hpp"
#include "json_input.hpp"
#include <functional>
#include <cctype>

namespace twisty::artifact {
namespace {
using namespace detail;
void normalize(Json &v, std::size_t depth = 0) {
  require(depth <= 64, "/", "Contract nesting exceeds 64.", "artifact.limit");
  if (v.is_number()) v = index(v);
  else if (v.is_array()) for (auto &x : v) normalize(x, depth + 1);
  else if (v.is_object()) for (auto &[key, x] : v.items()) {
    require(!key.empty() && std::isalpha(static_cast<unsigned char>(key[0])) &&
            std::all_of(key.begin(), key.end(), [](unsigned char c) {
              return c < 128 && std::isalnum(c);
            }), "/", "Contract property names must be ASCII identifiers.");
    normalize(x, depth + 1);
  }
}
std::vector<bool> state_only_roots(const core::ProgramData &d) {
  std::vector<bool> types(d.predicates.size() + 2, true);
  for (std::size_t i = 0; i < d.predicates.size(); ++i) {
    const auto &node = d.predicates[i];
    if (node.opcode == core::Opcode::in) types[i + 2] = node.bank == core::Bank::state;
    else for (Index c = 0; c < node.child_count; ++c)
      if (!types[d.child_references[node.first_child + c]]) types[i + 2] = false;
  }
  return types;
}
core::ProgramData decode_program(const Json &p) {
  fields(p, {"predicateVersion", "transportVersion", "pieceCount", "placementCount", "initialPlacements",
    "finiteSets", "predicateNodes", "childReferences", "invariantRoot", "goalRoot", "actionRecords", "requestPrograms", "compact"});
  require(p.at("predicateVersion") == 1 && p.at("transportVersion") == 1, "/program", "Unsupported program encoding.", "artifact.unsupported");
  core::ProgramData d;
  d.piece_count = index(p.at("pieceCount")); d.placement_count = index(p.at("placementCount"));
  d.initial = indices(p.at("initialPlacements"), 4096);
  for (const auto &set : array(p.at("finiteSets"))) d.finite_sets.push_back(indices(set));
  d.child_references = indices(p.at("childReferences"));
  d.invariant = index(p.at("invariantRoot")); d.goal = index(p.at("goalRoot"));
  for (const auto &node : array(p.at("predicateNodes"))) {
    core::Predicate n;
    require(node.is_object() && node.contains("op"), "/program/predicateNodes", "Missing predicate opcode.");
    if (node.at("op") == "IN") {
      fields(node, {"op", "bank", "input", "set"});
      require(node.at("bank") == "state" || node.at("bank") == "choice", "/program/predicateNodes", "Unknown predicate bank.");
      n.bank = node.at("bank") == "state" ? core::Bank::state : core::Bank::choice;
      n.input = index(node.at("input")); n.set = index(node.at("set"));
    } else {
      require(node.at("op") == "RANGE", "/program/predicateNodes", "Unknown predicate opcode.");
      fields(node, {"op", "min", "max", "firstChild", "childCount"});
      n.opcode = core::Opcode::range; n.lower = index(node.at("min")); n.upper = index(node.at("max"));
      n.first_child = index(node.at("firstChild")); n.child_count = index(node.at("childCount"));
    }
    d.predicates.push_back(n);
  }
  for (const auto &a : array(p.at("actionRecords"))) {
    fields(a, {"piece", "map", "selectionRoot", "event"});
    d.actions.push_back({index(a.at("piece")), index(a.at("map")), index(a.at("selectionRoot")), index(a.at("event"))});
  }
  for (const auto &r : array(p.at("requestPrograms"), 4096)) {
    fields(r, {"choiceSizes", "firstAction", "actionCount", "relationRoot"});
    d.requests.push_back({indices(r.at("choiceSizes"), 4096), index(r.at("firstAction")), index(r.at("actionCount")), index(r.at("relationRoot"))});
  }
  const auto &compact = p.at("compact");
  fields(compact, {"groups", "actions", "collections", "spaces", "positionMaps", "bindings", "families", "maps"});
  for (const auto &c : array(compact.at("collections"))) require(!c.contains("id"), "/program/compact", "Source collection labels are not executable fields.");
  runtime::read_compact(compact, d, 3);
  return d;
}
runtime::Bindings decode_bindings(const Json &b, const core::Program &program) {
  fields(b, {"puzzleId", "pieces", "placements", "events", "choiceSlots", "choiceValues", "requests"});
  runtime::Bindings out;
  out.puzzle_id = id(b.at("puzzleId")); out.pieces = ids(b.at("pieces"), 4096);
  out.placements = ids(b.at("placements")); out.events = ids(b.at("events"));
  out.choice_slots = ids(b.at("choiceSlots"), 4096);
  for (const auto &v : array(b.at("choiceValues"), 4096)) out.choice_values.push_back(ids(v));
  const auto &d = program.data();
  const auto state_roots = state_only_roots(d);
  require(out.pieces.size() == d.piece_count && out.placements.size() == d.placement_count &&
          out.choice_slots.size() == out.choice_values.size(), "/bindings", "Symbol-table dimensions differ from the program.", "artifact.reference");
  for (const auto &a : d.actions) require(a.event < out.events.size(), "/bindings/events", "Unknown event identity.", "artifact.reference");
  const auto &requests = array(b.at("requests"), 4096);
  require(requests.size() == d.requests.size(), "/bindings/requests", "Request binding count differs.", "artifact.reference");
  std::set<std::string> request_ids;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const auto &r = requests[i];
    fields(r, {"id", "profiles", "defaultProfile"});
    runtime::RequestBinding request{id(r.at("id")), {}, {}};
    require(request_ids.insert(request.id).second, "/bindings/requests", "Duplicate request ID.");
    require(d.requests[i].choice_sizes.size() == out.choice_values.size(), "/bindings/requests", "Choice-slot arity differs.");
    for (std::size_t slot = 0; slot < out.choice_values.size(); ++slot)
      require(out.choice_values[slot].size() == d.requests[i].choice_sizes[slot], "/bindings/choiceValues", "Choice dimensions differ.");
    std::set<std::string> profile_ids;
    for (const auto &p : array(r.at("profiles"), 4096)) {
      fields(p, {"id", "choices", "whenRoot"});
      runtime::Profile profile{id(p.at("id")), indices(p.at("choices"), 4096), index(p.at("whenRoot"))};
      require(profile_ids.insert(profile.id).second && profile.choices.size() == out.choice_values.size(), "/bindings/profiles", "Duplicate profile or incorrect arity.");
      for (std::size_t slot = 0; slot < profile.choices.size(); ++slot)
        require(profile.choices[slot] < out.choice_values[slot].size(), "/bindings/profiles", "Unknown profile choice.", "artifact.reference");
      require(profile.when < state_roots.size() && state_roots[profile.when], "/bindings/profiles", "Profile conditions must depend only on state.", "artifact.reference");
      request.profiles.push_back(std::move(profile));
    }
    if (!r.at("defaultProfile").is_null()) {
      request.default_profile = index(r.at("defaultProfile"));
      require(*request.default_profile < request.profiles.size(), "/bindings/defaultProfile", "Unknown default profile.", "artifact.reference");
    }
    out.requests.push_back(std::move(request));
  }
  return out;
}
} // namespace

static Json parse_bounded(const std::string &text, bool integers) {
  require(text.size() <= 64 * 1024 * 1024, "/", "Artifact text exceeds 64 MiB.", "artifact.limit");
  twisty::detail::check_json_depth(text, 64, [] { throw DiagnosticError("artifact.limit", "/", "Contract nesting exceeds 64."); });
  std::vector<std::set<std::string>> objects;
  std::size_t budget = 0;
  try {
    auto doc = Json::parse(text, [&](int, Json::parse_event_t event, Json &value) {
      budget += 64;
      if (value.is_string()) budget += value.get_ref<const std::string &>().size();
      require(budget <= 256 * 1024 * 1024, "/", "Contract DOM allocation budget exceeded.", "artifact.limit");
      if (event == Json::parse_event_t::object_start) objects.emplace_back();
      else if (event == Json::parse_event_t::object_end) objects.pop_back();
      else if (event == Json::parse_event_t::key)
        require(objects.back().insert(value.get<std::string>()).second, "/", "Duplicate JSON object key.");
      return true;
    });
    if (integers) normalize(doc);
    return doc;
  } catch (const Json::exception &error) { throw DiagnosticError("artifact.invalid", "/", error.what()); }
}
Json parse_document(const std::string &text) { return parse_bounded(text, true); }
Json detail::parse_geometry_document(const std::string &text) { return parse_bounded(text, false); }
std::string canonical_json(const Json &value) {
  auto copy = value; normalize(copy);
  try { return copy.dump(-1, ' ', false, Json::error_handler_t::strict); }
  catch (const Json::exception &error) { throw DiagnosticError("artifact.invalid", "/", error.what()); }
}
std::string document_digest(const Json &value, const std::string &own) {
  auto body = value; body.erase(own); return sha256(canonical_json(body));
}
void seal(Json &value, const std::string &own) { value[own] = document_digest(value, own); }
Json encode_program(const core::ProgramData &d) {
  Json out{{"predicateVersion", 1}, {"transportVersion", 1}, {"pieceCount", d.piece_count},
    {"placementCount", d.placement_count}, {"initialPlacements", d.initial}, {"finiteSets", d.finite_sets},
    {"childReferences", d.child_references}, {"invariantRoot", d.invariant}, {"goalRoot", d.goal},
    {"predicateNodes", Json::array()}, {"actionRecords", Json::array()}, {"requestPrograms", Json::array()},
    {"compact", runtime::write_compact(d, 3)}};
  for (const auto &n : d.predicates) out["predicateNodes"].push_back(n.opcode == core::Opcode::in
    ? Json{{"op", "IN"}, {"bank", n.bank == core::Bank::state ? "state" : "choice"}, {"input", n.input}, {"set", n.set}}
    : Json{{"op", "RANGE"}, {"min", n.lower}, {"max", n.upper}, {"firstChild", n.first_child}, {"childCount", n.child_count}});
  for (const auto &a : d.actions) out["actionRecords"].push_back({{"piece", a.piece}, {"map", a.map}, {"selectionRoot", a.selection}, {"event", a.event}});
  for (const auto &r : d.requests) out["requestPrograms"].push_back({{"choiceSizes", r.choice_sizes}, {"firstAction", r.first_action}, {"actionCount", r.action_count}, {"relationRoot", r.relation}});
  return out;
}
Json encode(const runtime::CompiledPuzzle &p, const std::vector<Index> &ri, const std::vector<std::vector<Index>> &ci) {
  const auto &b = p.bindings;
  Json bindings{{"puzzleId", b.puzzle_id}, {"pieces", b.pieces}, {"placements", b.placements}, {"events", b.events},
    {"choiceSlots", b.choice_slots}, {"choiceValues", b.choice_values}, {"requests", Json::array()}};
  for (const auto &r : b.requests) {
    Json profiles = Json::array();
    for (const auto &profile : r.profiles) profiles.push_back({{"id", profile.id}, {"choices", profile.choices}, {"whenRoot", profile.when}});
    bindings["requests"].push_back({{"id", r.id}, {"profiles", profiles}, {"defaultProfile", r.default_profile ? Json(*r.default_profile) : Json(nullptr)}});
  }
  Json out{{"kind", "twisty-abstract-artifact"}, {"artifactVersion", 1}, {"requiredCapabilities", Json::array()},
    {"program", encode_program(p.program.data())}, {"bindings", bindings},
    {"session", {{"requestInverses", ri}, {"choiceInverses", ci}}}};
  seal(out, "executionDigest");
  // Exporters pass through the same independent validator as consumers.
  Artifact verified(canonical_json(out));
  return verified.document();
}
Artifact::Artifact(const std::string &text) {
  try {
    document_ = parse_document(text);
    fields(document_, {"kind", "artifactVersion", "requiredCapabilities", "executionDigest", "program"}, {"bindings", "session"});
    require(document_.at("kind") == "twisty-abstract-artifact" && document_.at("artifactVersion") == 1, "/", "Unsupported artifact kind/version.", "artifact.unsupported");
    require(ids(document_.at("requiredCapabilities"), 64).empty(), "/requiredCapabilities", "Unsupported required artifact extension.", "artifact.capability");
    pin(document_, "executionDigest");
    core::Program program(decode_program(document_.at("program")));
    runtime::Bindings bindings;
    if (document_.contains("bindings")) bindings = decode_bindings(document_.at("bindings"), program);
    if (document_.contains("session")) {
      require(document_.contains("bindings"), "/session", "Session capability requires symbolic bindings.");
      const auto &s = document_.at("session"); fields(s, {"requestInverses", "choiceInverses"});
      request_inverses_ = indices(s.at("requestInverses"), 4096);
      for (const auto &row : array(s.at("choiceInverses"), 4096)) choice_inverses_.push_back(indices(row));
      require(request_inverses_.size() == bindings.requests.size() && choice_inverses_.size() == bindings.choice_values.size(), "/session", "Inverse-table dimensions differ.");
      auto reciprocal = [](const auto &v) {
        for (std::size_t i = 0; i < v.size(); ++i)
          require(v[i] < v.size() && v[v[i]] == i, "/session", "Inverse table is not reciprocal.", "artifact.reference");
      };
      reciprocal(request_inverses_);
      for (std::size_t slot = 0; slot < choice_inverses_.size(); ++slot) {
        require(choice_inverses_[slot].size() == bindings.choice_values[slot].size(), "/session", "Local inverse dimension differs.");
        reciprocal(choice_inverses_[slot]);
      }
      has_session_ = true;
    }
    compiled_ = std::make_shared<runtime::CompiledPuzzle>(runtime::CompiledPuzzle{std::move(program), std::move(bindings), 3});
  } catch (const core::Error &error) { throw DiagnosticError("artifact.invalid", "/program", error.what()); }
  catch (const Json::exception &error) { throw DiagnosticError("artifact.invalid", "/", error.what()); }
}
Json info() {
  return {{"artifactAPIVersion", 1}, {"artifactVersions", {1}}, {"interpretationVersions", {1}},
    {"sessionSchemaVersions", {3}}, {"witnessVersions", {2}}, {"frameBufferVersion", 1},
    {"realizationKinds", {"artifact-cube-euclidean"}},
    {"geometryCapabilities", {"uniform-cube-ports", "explicit-cube-tracks"}}};
}
std::string validate_json(const std::string &text) {
  try {
    const Artifact artifact(text);
    return Json{{"status", "Valid"}, {"executionDigest", artifact.document().at("executionDigest")},
      {"session", artifact.has_session()}}.dump();
  } catch (const std::exception &error) { return diagnostic_result(error).dump(); }
}
} // namespace twisty::artifact
