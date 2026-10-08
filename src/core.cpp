#include "twisty/core.hpp"
#include <algorithm>
#include <set>

namespace twisty {
DiagnosticError::DiagnosticError(std::string c, std::string s, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(c)), source(std::move(s)) {}
Json DiagnosticError::json() const {
  return {{"reasonCode", code}, {"source", source}, {"message", what()}};
}
Json diagnostic_result(const std::exception &error) {
  const auto *diagnostic = dynamic_cast<const DiagnosticError *>(&error);
  return {{"status", "Invalid"},
          {"diagnostics", Json::array({diagnostic ? diagnostic->json()
                                                  : Json{{"reasonCode", "document.invalid"},
                                                         {"source", "/"},
                                                         {"message", error.what()}}})}};
}
void require_exact(const Json &value, const std::string &path) {
  if (value.is_number_float())
    throw DiagnosticError("value.inexact", path,
                          "Logical values must be exact; floating-point values are not supported.");
  if (value.is_structured())
    for (const auto &[key, child] : value.items())
      require_exact(child, path + "/" + key);
}
namespace {
void ensure(bool condition, const std::string &code, const std::string &source, const std::string &message) {
  if (!condition)
    throw DiagnosticError(code, source, message);
}
void sort_by_id(Json &array, const std::string &key) {
  std::sort(array.begin(), array.end(), [&](const Json &a, const Json &b) {
    return a.at(key).get<std::string>() < b.at(key).get<std::string>();
  });
}
} // namespace

std::shared_ptr<const Definition> load_definition(Json compiled) {
  require_exact(compiled, "/");
  ensure(compiled.at("schemaVersion") == 1, "schema.unsupported", "/schemaVersion",
         "Only schema version 1 is supported.");
  ensure(compiled.at("kind") == "finite-definition", "definition.unsupported", "/kind",
         "Expected a finite definition.");
  ensure(compiled.at("ruleModule") == "finite-footprint@1", "rule.unsupported", "/ruleModule",
         "Unknown abstract rule module.");
  ensure(compiled.at("goal").at("kind") == "home", "goal.unsupported", "/goal",
         "Only the home-placement goal is supported.");
  ensure(compiled.at("pieces").size() <= 4096 && compiled.at("placementDomains").size() <= 128,
         "definition.limit", "/", "Definition exceeds the initial finite model limits.");
  const auto claimed_digest = compiled.value("definitionDigest", std::string{});
  auto definition = std::make_shared<Definition>();
  definition->provenance = compiled.value("provenance", Json::object());
  compiled.erase("provenance");
  compiled.erase("definitionDigest");
  compiled.erase("displayName");
  sort_by_id(compiled["pieceTypes"], "id");
  sort_by_id(compiled["pieces"], "id");
  sort_by_id(compiled["placementDomains"], "id");
  sort_by_id(compiled["operations"], "id");
  std::sort(compiled["cells"].begin(), compiled["cells"].end());
  for (auto &domain : compiled["placementDomains"]) {
    sort_by_id(domain["placements"], "key");
    for (auto &q : domain["placements"])
      std::sort(q["footprint"].begin(), q["footprint"].end());
  }
  for (auto &operation : compiled["operations"])
    std::sort(operation["selectedCells"].begin(), operation["selectedCells"].end());
  definition->id = compiled.at("puzzleId").get<std::string>();
  definition->canonical = compiled;
  definition->digest = sha256(compiled.dump());
  ensure(claimed_digest.empty() || claimed_digest == definition->digest, "definition.digest_mismatch",
         "/definitionDigest", "Compiled definition digest does not match its contents.");
  std::set<std::string> cells;
  for (const auto &cell : compiled.at("cells"))
    ensure(cells.insert(cell.get<std::string>()).second, "cell.duplicate", "/cells",
           "Duplicate abstract cell.");
  std::map<std::string, Index> domains;
  for (const auto &item : compiled.at("placementDomains")) {
    Domain domain;
    domain.id = item.at("id").get<std::string>();
    ensure(domains.emplace(domain.id, static_cast<Index>(definition->domains.size())).second,
           "domain.duplicate", domain.id, "Duplicate placement domain.");
    ensure(!item.at("placements").empty() && item.at("placements").size() <= 65536, "domain.limit", domain.id,
           "Placement domain is empty or exceeds the finite limit.");
    for (const auto &q : item.at("placements")) {
      Placement placement;
      placement.key = q.at("key").get<std::string>();
      placement.footprint = q.at("footprint").get<std::vector<std::string>>();
      ensure(!placement.footprint.empty(), "placement.empty", placement.key,
             "A finite placement must occupy a cell.");
      std::set<std::string> occupied;
      for (const auto &cell : placement.footprint)
        ensure(cells.contains(cell) && occupied.insert(cell).second, "placement.cell", placement.key,
               "Footprints must reference distinct declared cells.");
      for (const auto &[port, attachment] : q.at("portAttachment").items()) {
        const auto cell = attachment.at("cell").get<std::string>();
        ensure(occupied.contains(cell), "port.cell", placement.key,
               "A port must attach to an occupied cell.");
        placement.ports.emplace(port, std::pair{cell, attachment.at("attachment").get<std::string>()});
      }
      ensure(domain.by_key.emplace(placement.key, static_cast<Index>(domain.placements.size())).second,
             "placement.duplicate", placement.key, "Duplicate placement key.");
      domain.placements.push_back(std::move(placement));
    }
    definition->domains.push_back(std::move(domain));
  }
  std::map<std::string, Index> types;
  std::map<std::string, std::set<std::string>> type_ports;
  for (const auto &type : compiled.at("pieceTypes")) {
    const auto id = type.at("id").get<std::string>();
    const auto domain = type.at("placementDomainId").get<std::string>();
    ensure(domains.contains(domain) && types.emplace(id, domains.at(domain)).second, "type.domain", id,
           "Piece type must name a declared domain and have a unique ID.");
    std::set<std::string> ports;
    for (const auto &port : type.at("localPorts"))
      ensure(ports.insert(port.get<std::string>()).second, "port.duplicate", id,
             "Local ports must have unique IDs.");
    type_ports[id] = ports;
    for (const auto &placement : definition->domains.at(domains.at(domain)).placements) {
      std::set<std::string> attached;
      for (const auto &[port, _] : placement.ports)
        attached.insert(port);
      ensure(attached == ports, "port.missing", placement.key,
             "Every local port must have exactly one attachment.");
    }
  }
  std::set<std::string> piece_ids;
  for (const auto &item : compiled.at("pieces")) {
    const auto id = item.at("id").get<std::string>();
    const auto type = item.at("type").get<std::string>();
    ensure(piece_ids.insert(id).second && types.contains(type), "piece.type", id,
           "Piece IDs must be unique and types must be declared.");
    const auto domain = types.at(type);
    const auto home = item.at("homePlacement").get<std::string>();
    ensure(definition->domains[domain].by_key.contains(home), "piece.home", id,
           "Home placement is outside the piece's domain.");
    auto labels = item.at("portLabels").get<std::map<std::string, std::string>>();
    std::set<std::string> label_ports;
    for (const auto &[port, _] : labels)
      label_ports.insert(port);
    ensure(label_ports == type_ports.at(type), "piece.labels", id,
           "Port labels must cover every local port.");
    definition->pieces.push_back(
        {id, type, domain, definition->domains[domain].by_key.at(home), std::move(labels)});
  }
  for (const auto &item : compiled.at("operations")) {
    Operation operation;
    operation.id = item.at("id").get<std::string>();
    operation.inverse = item.at("inverse").get<std::string>();
    operation.family = item.at("family").get<std::string>();
    operation.transport = item.at("transport").get<std::string>();
    operation.cells = item.at("selectedCells").get<std::vector<std::string>>();
    std::set<std::string> selected;
    for (const auto &cell : operation.cells)
      ensure(cells.contains(cell) && selected.insert(cell).second, "operation.cell", operation.id,
             "Operation selects an invalid or duplicate cell.");
    operation.mechanism_guard = item.value("mechanismGuard", Json::object());
    operation.mechanism_update = item.value("mechanismUpdate", Json::object());
    ensure(operation.mechanism_guard.is_object() && operation.mechanism_update.is_object(),
           "mechanism.record", operation.id, "Mechanism guards and updates must be records.");
    for (const auto &domain : definition->domains) {
      const auto &map = item.at("transports").at(domain.id);
      ensure(map.size() == domain.placements.size(), "transport.coverage", operation.id,
             "Transport table must cover its finite domain.");
      std::vector<Index> indexes;
      std::set<Index> destinations;
      for (const auto &placement : domain.placements) {
        const auto destination = map.at(placement.key).get<std::string>();
        ensure(domain.by_key.contains(destination), "transport.target", operation.id,
               "Transport target is outside the domain.");
        const auto index = domain.by_key.at(destination);
        ensure(destinations.insert(index).second, "transport.bijection", operation.id,
               "Transport table must be bijective.");
        indexes.push_back(index);
        const auto contained = std::all_of(placement.footprint.begin(), placement.footprint.end(),
                                           [&](const auto &cell) { return selected.contains(cell); });
        if (contained)
          for (const auto &cell : domain.placements[index].footprint)
            ensure(selected.contains(cell), "transport.support", operation.id,
                   "Participating placements must remain in the selected cells.");
      }
      operation.maps.push_back(std::move(indexes));
    }
    ensure(definition->operation_ids.emplace(operation.id, static_cast<Index>(definition->operations.size()))
               .second,
           "operation.duplicate", operation.id, "Duplicate operation ID.");
    definition->operations.push_back(std::move(operation));
  }
  for (const auto &operation : definition->operations) {
    ensure(definition->operation_ids.contains(operation.inverse), "inverse.missing", operation.id,
           "Declared inverse operation does not exist.");
    const auto &opposite = definition->operations.at(definition->operation_ids.at(operation.inverse));
    ensure(opposite.inverse == operation.id && opposite.cells == operation.cells, "inverse.support",
           operation.id, "Inverse declarations and cell selection must agree.");
    for (const auto &[key, value] : operation.mechanism_update.items()) {
      ensure(operation.mechanism_guard.contains(key) && opposite.mechanism_guard.contains(key) &&
                 opposite.mechanism_guard.at(key) == value && opposite.mechanism_update.contains(key) &&
                 opposite.mechanism_update.at(key) == operation.mechanism_guard.at(key),
             "inverse.mechanism", operation.id, "Every mechanism update must have an exact guarded inverse.");
    }
    for (const auto &[key, value] : operation.mechanism_guard.items())
      if (!operation.mechanism_update.contains(key))
        ensure(opposite.mechanism_guard.contains(key) && opposite.mechanism_guard.at(key) == value,
               "inverse.mechanism", operation.id, "Unchanged mechanism guards must agree with the inverse.");
    for (Index domain = 0; domain < definition->domains.size(); ++domain)
      for (Index q = 0; q < operation.maps[domain].size(); ++q)
        ensure(opposite.maps[domain][operation.maps[domain][q]] == q, "inverse.transport", operation.id,
               "Declared transport inverse does not restore the placement.");
  }
  definition->initial = decode_state(*definition, compiled.at("initialState"));
  return definition;
}

Json encode_state(const Definition &definition, const State &state) {
  Json placement = Json::object();
  for (Index i = 0; i < definition.pieces.size(); ++i) {
    const auto &piece = definition.pieces[i];
    placement[piece.id] = definition.domains[piece.domain].placements.at(state.placement_of.at(i)).key;
  }
  return {{"placementOf", placement}, {"mechanism", state.mechanism}};
}
Json validate_state(const Definition &definition, const State &state) {
  Json diagnostics = Json::array();
  if (state.placement_of.size() != definition.pieces.size())
    return Json::array({{{"reasonCode", "state.piece_count"},
                         {"source", "/placementOf"},
                         {"message", "State must contain every persistent piece."}}});
  try {
    require_exact(state.mechanism, "/mechanism");
  } catch (const DiagnosticError &error) {
    diagnostics.push_back(error.json());
  }
  if (!state.mechanism.is_object())
    diagnostics.push_back(
        {{"reasonCode", "state.mechanism"}, {"message", "Mechanism variables must be a record."}});
  std::map<std::string, std::vector<std::string>> occupants;
  for (const auto &cell : definition.canonical.at("cells"))
    occupants[cell.get<std::string>()] = {};
  for (Index i = 0; i < definition.pieces.size(); ++i) {
    const auto &piece = definition.pieces[i];
    if (state.placement_of[i] >= definition.domains[piece.domain].placements.size()) {
      diagnostics.push_back({{"reasonCode", "state.placement"},
                             {"source", piece.id},
                             {"message", "Placement is outside the piece domain."}});
      continue;
    }
    for (const auto &cell : definition.domains[piece.domain].placements[state.placement_of[i]].footprint)
      occupants[cell].push_back(piece.id);
  }
  for (const auto &[cell, pieces] : occupants)
    if (pieces.size() != 1)
      diagnostics.push_back({{"reasonCode", "state.occupancy"},
                             {"source", cell},
                             {"implicatedPieces", pieces},
                             {"message", "Each declared cell must have exactly one occupant."}});
  return diagnostics;
}
State decode_state(const Definition &definition, const Json &json) {
  State state;
  state.mechanism = json.at("mechanism");
  if (!json.at("placementOf").is_object() || json.at("placementOf").size() != definition.pieces.size())
    throw DiagnosticError("state.piece_count", "/placementOf",
                          "State must contain exactly the definition's persistent pieces.");
  for (const auto &piece : definition.pieces) {
    const auto key = json.at("placementOf").at(piece.id).get<std::string>();
    const auto &domain = definition.domains[piece.domain];
    if (!domain.by_key.contains(key))
      throw DiagnosticError("state.placement", piece.id, "Unknown placement key for piece type.");
    state.placement_of.push_back(domain.by_key.at(key));
  }
  const auto diagnostics = validate_state(definition, state);
  if (!diagnostics.empty())
    throw DiagnosticError(diagnostics[0].at("reasonCode"), diagnostics[0].value("source", "/"),
                          diagnostics[0].at("message"));
  return state;
}
std::string state_digest(const Definition &definition, const State &state) {
  return sha256(
      Json{{"definitionDigest", definition.digest}, {"state", encode_state(definition, state)}}.dump());
}
bool solved(const Definition &definition, const State &state) {
  for (Index i = 0; i < definition.pieces.size(); ++i)
    if (state.placement_of[i] != definition.pieces[i].home)
      return false;
  return state.mechanism == definition.initial.mechanism;
}
Plan plan(const Definition &definition, const State &state, const std::string &id) {
  if (!definition.operation_ids.contains(id))
    throw DiagnosticError("operation.unknown", id, "Unknown directed operation.");
  const auto diagnostics = validate_state(definition, state);
  if (!diagnostics.empty())
    throw DiagnosticError("state.invalid", "/", diagnostics.dump());
  const auto &operation = definition.operations.at(definition.operation_ids.at(id));
  for (const auto &[key, value] : operation.mechanism_guard.items())
    if (!state.mechanism.contains(key) || state.mechanism.at(key) != value)
      return {std::nullopt,
              {{"status", "Blocked"},
               {"reasonCode", "mechanism.disabled"},
               {"operationId", id},
               {"constraintId", "mechanism.guard"},
               {"implicatedPieces", Json::array()},
               {"evidence", {{"variable", key}, {"required", value}}}}};
  Transition transition{id, state, state, {}};
  const std::set<std::string> selected(operation.cells.begin(), operation.cells.end());
  for (Index i = 0; i < definition.pieces.size(); ++i) {
    const auto &piece = definition.pieces[i];
    const auto from = state.placement_of[i];
    const auto &placement = definition.domains[piece.domain].placements[from];
    std::vector<std::string> overlap;
    for (const auto &cell : placement.footprint)
      if (selected.contains(cell))
        overlap.push_back(cell);
    if (!overlap.empty() && overlap.size() != placement.footprint.size())
      return {std::nullopt,
              {{"status", "Blocked"},
               {"reasonCode", "footprint.partial_overlap"},
               {"operationId", id},
               {"constraintId", "rigid-footprint"},
               {"implicatedPieces", Json::array({piece.id})},
               {"implicatedPlacements", Json::array({placement.key})},
               {"evidence",
                {{"footprint", placement.footprint},
                 {"selectedCells", operation.cells},
                 {"intersection", overlap}}}}};
    if (!overlap.empty()) {
      const auto to = operation.maps[piece.domain][from];
      transition.after.placement_of[i] = to;
      transition.actions.push_back({i, from, to});
    }
  }
  for (const auto &[key, value] : operation.mechanism_update.items())
    transition.after.mechanism[key] = value;
  const auto after_errors = validate_state(definition, transition.after);
  if (!after_errors.empty())
    throw DiagnosticError("operation.postcondition", id, after_errors.dump());
  return {transition, Json{}};
}
Json encode_transition(const Definition &definition, const Transition &transition) {
  const auto &operation = definition.operations.at(definition.operation_ids.at(transition.operation));
  Json actions = Json::array();
  for (const auto &action : transition.actions) {
    const auto &piece = definition.pieces[action.piece];
    const auto &domain = definition.domains[piece.domain];
    actions.push_back({{"pieceId", piece.id},
                       {"from", domain.placements[action.from].key},
                       {"to", domain.placements[action.to].key},
                       {"role", "layer-participant"},
                       {"transport", operation.transport}});
  }
  return {{"definitionDigest", definition.digest},
          {"beforeStateDigest", state_digest(definition, transition.before)},
          {"afterStateDigest", state_digest(definition, transition.after)},
          {"request", {{"operation", transition.operation}, {"parameters", Json::object()}}},
          {"pieceActions", actions},
          {"mechanismChanges", Json::diff(transition.before.mechanism, transition.after.mechanism)},
          {"beforeState", encode_state(definition, transition.before)},
          {"afterState", encode_state(definition, transition.after)}};
}
Json legal_operations(const Definition &definition, const State &state) {
  Json requests = Json::array();
  for (const auto &operation : definition.operations)
    if (plan(definition, state, operation.id).transition)
      requests.push_back({{"operation", operation.id}, {"parameters", Json::object()}});
  return requests;
}
} // namespace twisty
