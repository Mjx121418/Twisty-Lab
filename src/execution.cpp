#include "twisty/execution.hpp"
#include <algorithm>
#include <set>

namespace twisty {
Json compile_execution_json(const std::string &text) {
  try {
    return {{"status", "Compiled"}, {"definition", Execution::from_source(text)->definition_json()},
            {"diagnostics", Json::array()}};
  } catch (const flat::Error &error) {
    return {{"status", "Invalid"},
            {"diagnostics",
             Json::array({{{"reasonCode", error.code}, {"source", "/"}, {"message", error.what()}}})}};
  } catch (const std::exception &error) {
    return diagnostic_result(error);
  }
}
namespace {
void ensure(bool condition, const std::string &message) {
  if (!condition)
    throw DiagnosticError("bindings.invalid", "/catalog", message);
}
template <class T> Index index_of(const std::vector<T> &items, const T &item) {
  const auto it = std::find(items.begin(), items.end(), item);
  ensure(it != items.end(), "Binding references an unknown symbol.");
  return static_cast<Index>(it - items.begin());
}
} // namespace

std::shared_ptr<const Execution> Execution::legacy(std::shared_ptr<const Definition> definition) {
  if (definition->canonical.value("kind", std::string{}) != "finite-definition")
    throw DiagnosticError("execution.context_required", "/definition",
                          "Relational port metadata is not a legacy execution definition; pass "
                          "Session::execution() or definition_json().");
  return std::shared_ptr<const Execution>(new Execution(std::move(definition)));
}
std::shared_ptr<const Execution> Execution::from_source(const std::string &text) {
  if (text.size() > 16 * 1024 * 1024)
    throw DiagnosticError("document.limit", "/", "Execution documents are limited to 16 MiB.");
  const auto document = Json::parse(text, [](int depth, Json::parse_event_t, Json &) {
    if (depth > 512)
      throw DiagnosticError("document.depth", "/", "Execution document nesting exceeds 512 levels.");
    return true;
  });
  const auto kind = document.value("kind", std::string{});
  if (kind == "relational-package" || kind == "relational-definition") {
    if (document.at("formatVersion") != 1)
      throw DiagnosticError("schema.unsupported", "/formatVersion", "Expected relational format version 1.");
    auto execution =
        std::shared_ptr<const Execution>(new Execution(document.at("source"), document.at("catalog")));
    if (kind == "relational-definition" && execution->definition_json() != document)
      throw DiagnosticError("definition.digest_mismatch", "/",
                            "Relational definition differs from its compiled contents.");
    return execution;
  }
  if (kind == "finite-definition")
    return legacy(load_definition(document));
  const auto result = compile_json(text);
  if (result.at("status") != "Compiled") {
    const auto &d = result.at("diagnostics").at(0);
    throw DiagnosticError(d.at("reasonCode").get<std::string>(), d.value("source", "/"),
                          d.at("message").get<std::string>());
  }
  return legacy(load_definition(result.at("definition")));
}

Execution::Execution(const Json &source, const Json &catalog) : puzzle_(relational::compile(source)) {
  // This adapter translates the already parity-gated cube port catalog into the
  // existing interpreter's grouped frame tables. It does not compile any rules.
  ensure(catalog.at("kind") == "relational-cube-bindings" && catalog.at("version") == 1,
         "This interpreter adapter requires cube binding catalog version 1.");
  const auto &bindings = puzzle_->bindings;
  ensure(catalog.at("puzzleId").get<std::string>() == bindings.puzzle_id, "Puzzle IDs differ.");
  auto definition = std::make_shared<Definition>();
  definition->id = bindings.puzzle_id;
  definition->cells = catalog.at("resources").get<std::vector<std::string>>();
  ensure(catalog.at("domains").size() == source.at("domains").size(), "Domain counts differ.");
  local_of_global_.resize(bindings.placements.size());
  inverse_moves_.resize(bindings.choice_slots.size());
  Json types = Json::array(), domains = Json::array(), pieces = Json::array(), operations = Json::array();
  std::map<std::string, Index> domain_ids;
  std::set<std::string> declared_placements, declared_pieces;
  for (Index d = 0; d < source.at("domains").size(); ++d) {
    const auto &s = source.at("domains").at(d), &c = catalog.at("domains").at(d);
    const auto id = s.at("id").get<std::string>();
    ensure(c.at("id").get<std::string>() == id, "Domain order differs.");
    domain_ids.emplace(id, d);
    Domain domain;
    domain.id = id;
    Json wire = Json::array();
    std::vector<Index> global;
    for (const auto &q : s.at("placements")) {
      const auto key = q.get<std::string>(), qualified = id + "/" + key;
      ensure(declared_placements.insert(qualified).second, "Duplicate placement binding.");
      const auto g = index_of(bindings.placements, qualified);
      local_of_global_[g] = static_cast<Index>(global.size());
      global.push_back(g);
      const auto &entry = catalog.at("placements").at(qualified);
      Placement placement;
      placement.key = key;
      placement.footprint = entry.at("resources").get<std::vector<std::string>>();
      for (const auto &cell : placement.footprint)
        placement.footprint_cells.push_back(index_of(definition->cells, cell));
      for (const auto &[port, p] : entry.at("ports").items()) {
        ensure(std::find(placement.footprint.begin(), placement.footprint.end(),
                         p.at("cell").get<std::string>()) != placement.footprint.end(),
               "Port is outside its resources.");
        placement.ports.emplace(
            port, std::pair{p.at("cell").get<std::string>(), p.at("attachment").get<std::string>()});
      }
      wire.push_back(
          {{"key", key}, {"footprint", placement.footprint}, {"portAttachment", entry.at("ports")}});
      domain.by_key.emplace(key, static_cast<Index>(domain.placements.size()));
      domain.placements.push_back(std::move(placement));
    }
    ensure(
        c.at("placements").get<std::vector<std::string>>() ==
            [&] {
              std::vector<std::string> q;
              for (auto g : global)
                q.push_back(bindings.placements[g]);
              return q;
            }(),
        "Placement binding order differs.");
    global_of_local_.push_back(std::move(global));
    definition->domains.push_back(std::move(domain));
    domains.push_back({{"id", id}, {"placements", wire}});
    types.push_back({{"id", id}, {"placementDomainId", id}, {"localPorts", c.at("localPorts")}});
    auto &inverses = inverse_moves_[d];
    inverses["idle"] = "idle";
    for (const auto &[op, metadata] : catalog.at("requests").items())
      inverses[op] = metadata.at("inverse").get<std::string>();
    std::vector<std::string> expected_pieces;
    for (const auto &p : s.at("pieces"))
      expected_pieces.push_back(p.at("id").get<std::string>());
    ensure(c.at("pieces").get<std::vector<std::string>>() == expected_pieces, "Piece binding order differs.");
  }
  ensure(declared_placements.size() == catalog.at("placements").size(), "Extraneous placement bindings.");
  // The legacy presentation tables are sorted by symbolic piece ID. Flat indices
  // are independently mapped, never inferred from that ordering.
  auto sorted_pieces = bindings.pieces;
  std::sort(sorted_pieces.begin(), sorted_pieces.end());
  display_piece_of_flat_.resize(sorted_pieces.size());
  for (const auto &id : sorted_pieces) {
    const auto p = index_of(bindings.pieces, id);
    flat_piece_of_display_.push_back(p);
    display_piece_of_flat_[p] = static_cast<Index>(definition->pieces.size());
    const auto &entry = catalog.at("pieces").at(id);
    const auto d = domain_ids.at(entry.at("domain").get<std::string>());
    const auto home = index_of(bindings.placements, entry.at("home").get<std::string>());
    ensure(std::find(global_of_local_[d].begin(), global_of_local_[d].end(), home) !=
                   global_of_local_[d].end() &&
               puzzle_->program.data().initial[p] == home,
           "Piece home/domain binding differs from the program.");
    Piece piece{id, definition->domains[d].id, d, local_of_global_[home],
                entry.at("portLabels").get<std::map<std::string, std::string>>()};
    for (const auto &[port, _] : piece.labels)
      for (const auto &q : definition->domains[d].placements)
        ensure(q.ports.contains(port), "Missing piece port binding.");
    definition->initial.placement_of.push_back(piece.home);
    pieces.push_back({{"id", id},
                      {"type", piece.type},
                      {"homePlacement", definition->domains[d].placements[piece.home].key},
                      {"portLabels", piece.labels}});
    definition->pieces.push_back(std::move(piece));
    declared_pieces.insert(id);
  }
  ensure(declared_pieces.size() == catalog.at("pieces").size(), "Extraneous piece bindings.");
  ensure(catalog.at("requests").size() == bindings.requests.size(), "Request binding counts differ.");
  for (Index r = 0; r < bindings.requests.size(); ++r) {
    const auto &request = bindings.requests[r];
    request_ids_.emplace(request.id, r);
    const auto &c = catalog.at("requests").at(request.id);
    const auto choices = relational::bind_profile(*puzzle_, r, puzzle_->program.data().initial);
    ensure(choices.has_value() && request.default_profile &&
               request.profiles.at(*request.default_profile).when == flat::true_ref,
           "The cube frame adapter requires a constant declared default profile.");
    Operation operation;
    operation.id = request.id;
    operation.inverse = c.at("inverse").get<std::string>();
    operation.family = c.at("family").get<std::string>();
    operation.transport = c.at("event").get<std::string>();
    ensure(request.profiles.at(*request.default_profile).id == c.at("profile").get<std::string>(),
           "Profile binding differs.");
    for (const auto &cell : definition->cells)
      if (cell.find(operation.family) != std::string::npos)
        operation.cells.push_back(cell);
    for (Index d = 0; d < source.at("domains").size(); ++d) {
      const auto movement = bindings.choice_values[d].at(choices->at(d));
      ensure(movement == request.id, "Cube track requires the same named move in every domain.");
      const auto &moves = source.at("domains").at(d).at("moves");
      const auto move = std::find_if(moves.begin(), moves.end(), [&](const auto &m) {
        return m.at("id").template get<std::string>() == movement;
      });
      ensure(move != moves.end() && move->at("event").template get<std::string>() == operation.transport,
             "Cube event binding differs from the movement.");
      std::vector<Index> map;
      const auto &domain = definition->domains[d];
      for (const auto &q : domain.placements) {
        const auto &transport = move->at("transport");
        map.push_back(transport.contains(q.key) ? domain.by_key.at(transport.at(q.key).get<std::string>())
                                                : domain.by_key.at(q.key));
      }
      operation.maps.push_back(std::move(map));
    }
    operations.push_back({{"id", operation.id},
                          {"inverse", operation.inverse},
                          {"family", operation.family},
                          {"transport", operation.transport},
                          {"selectedCells", operation.cells}});
    definition->operation_ids.emplace(operation.id, static_cast<Index>(definition->operations.size()));
    definition->operations.push_back(std::move(operation));
  }
  for (const auto &op : definition->operations)
    ensure(definition->operation_ids.contains(op.inverse) &&
               definition->operations.at(definition->operation_ids.at(op.inverse)).inverse == op.id,
           "Inverse requests must be reciprocal.");
  definition->provenance = {{"execution", "flat"}, {"adapter", "cube-ports@1"}};
  const auto preview = relational::inspect(*puzzle_);
  definition->canonical = {{"kind", "relational-definition"},
                           {"formatVersion", 1},
                           {"predicateVersion", 1},
                           {"source", source},
                           {"catalog", catalog},
                           {"program", preview.at("program")},
                           {"bindings", preview.at("bindings")},
                           {"puzzleId", definition->id},
                           {"ruleModule", "relational@1"},
                           {"cells", definition->cells},
                           {"pieceTypes", types},
                           {"placementDomains", domains},
                           {"pieces", pieces},
                           {"operations", operations},
                           {"symmetry", {{"members", catalog.at("symmetry")}}}};
  // Versioned canonical bytes cover the executable, completion policy, symbolic
  // mapping, inverse metadata, and port bindings. Old semantic pins cannot match.
  definition->digest = sha256(definition->canonical.dump());
  definition_ = std::move(definition);
}
Json Execution::definition_json() const {
  auto result = definition_->canonical;
  result["definitionDigest"] = definition_->digest;
  if (!relational())
    result["provenance"] = definition_->provenance;
  return result;
}
flat::State Execution::flatten(const State &state) const {
  if (state.placement_of.size() != definition_->pieces.size() || !state.mechanism.is_object() ||
      !state.mechanism.empty())
    throw DiagnosticError("state.invalid", "/state",
                          "Expected exactly the declared pieces and an empty mechanism record.");
  flat::State result(state.placement_of.size());
  for (Index p = 0; p < result.size(); ++p) {
    const auto &map = global_of_local_.at(definition_->pieces[p].domain);
    if (state.placement_of[p] >= map.size())
      throw DiagnosticError("state.placement", "/placementOf", "Placement is outside the declared catalog.");
    result[flat_piece_of_display_[p]] = map[state.placement_of[p]];
  }
  return result;
}
State Execution::display(const flat::State &state) const {
  State result;
  result.placement_of.resize(state.size());
  for (Index p = 0; p < state.size(); ++p)
    result.placement_of[display_piece_of_flat_[p]] = local_of_global_.at(state[p]);
  return result;
}
void Execution::validate(const State &state) const {
  if (!relational()) {
    const auto result = validate_state(*definition_, state);
    if (!result.empty())
      throw DiagnosticError("state.invalid", "/state", result.dump());
  } else if (!puzzle_->program.valid(flatten(state)))
    throw DiagnosticError("state.invariant", "/state", "State violates the compiled invariant.");
}
State Execution::decode(const Json &wire) const {
  if (!relational())
    return decode_state(*definition_, wire);
  const auto &placements = wire.at("placementOf");
  if (!placements.is_object() || placements.size() != definition_->pieces.size())
    throw DiagnosticError("state.pieces", "/placementOf", "Expected exactly the declared piece IDs.");
  State result;
  result.mechanism = wire.at("mechanism");
  for (const auto &p : definition_->pieces) {
    const auto key = placements.at(p.id).get<std::string>();
    const auto &map = definition_->domains[p.domain].by_key;
    if (!map.contains(key))
      throw DiagnosticError("state.placement", "/placementOf/" + p.id, "Unknown placement key.");
    result.placement_of.push_back(map.at(key));
  }
  validate(result);
  return result;
}
bool Execution::is_solved(const State &state) const {
  return relational() ? puzzle_->program.solved(flatten(state)) : solved(*definition_, state);
}
ExecutionPlan Execution::plan(const State &state, const std::string &operation,
                              const Json &named_choices) const {
  if (!relational()) {
    if (!named_choices.is_null())
      throw DiagnosticError("request.choices", "/choices", "Legacy requests do not accept movement tuples.");
    auto result = twisty::plan(*definition_, state, operation);
    if (!result.transition)
      return {std::nullopt, result.blocked};
    return {CompletedTransition{std::move(*result.transition), {}, {}}, {}};
  }
  validate(state);
  if (!request_ids_.contains(operation))
    throw DiagnosticError("request.unknown", "/operation", "Unknown request.");
  const auto request = request_ids_.at(operation);
  const auto before = flatten(state);
  std::optional<flat::Choices> choices;
  if (named_choices.is_null())
    choices = relational::bind_profile(*puzzle_, request, before);
  else {
    const auto names = named_choices.get<std::vector<std::string>>();
    if (names.size() != puzzle_->bindings.choice_slots.size())
      throw DiagnosticError("request.choices", "/choices",
                            "Supply one choice for every declared choice slot.");
    choices = flat::Choices{};
    for (Index i = 0; i < names.size(); ++i)
      choices->push_back(index_of(puzzle_->bindings.choice_values[i], names[i]));
  }
  if (!choices)
    return {std::nullopt, {{"status", "ChoiceRequired"}, {"operation", operation}}};
  const auto result = puzzle_->program.plan(request, before, *choices);
  if (!result.transition) {
    const auto reason = result.rejection == flat::Rejection::relation ? "relation.rejected"
                        : result.rejection == flat::Rejection::target_invariant
                            ? "state.target_invariant"
                            : "transport.conflicting_writes";
    // Dependency evidence belongs to this adapter, not to execution semantics.
    // These are relation inputs, not a minimal unsatisfiable set or a claim that
    // each referenced piece individually causes rejection.
    std::set<std::string> inputs;
    const auto &data = puzzle_->program.data();
    const auto root = data.requests[request].relation;
    std::vector<flat::Ref> pending{root};
    std::vector<bool> visited(data.predicates.size() + 2, false);
    while (!pending.empty()) {
      const auto ref = pending.back();
      pending.pop_back();
      if (ref < 2 || visited[ref])
        continue;
      visited[ref] = true;
      const auto &node = data.predicates[ref - 2];
      if (node.opcode == flat::Opcode::in && node.bank == flat::Bank::state)
        inputs.insert(puzzle_->bindings.pieces[node.input]);
      else if (node.opcode == flat::Opcode::range)
        for (Index i = 0; i < node.child_count; ++i)
          pending.push_back(data.child_references[node.first_child + i]);
    }
    return {std::nullopt,
            {{"status", "Blocked"},
             {"reasonCode", reason},
             {"operation", operation},
             {"scope", "completed-request"},
             {"implicatedPieces", inputs},
             {"evidence", {{"kind", "relation-inputs"}, {"relationRoot", root}}}}};
  }
  const auto &w = *result.transition;
  CompletedTransition transition{{operation, state, display(w.after), {}}, {}, {}};
  for (Index i = 0; i < choices->size(); ++i)
    transition.choices.push_back(puzzle_->bindings.choice_values[i][choices->at(i)]);
  for (const auto &a : w.actions) {
    transition.actions.push_back(
        {display_piece_of_flat_[a.piece], local_of_global_[a.from], local_of_global_[a.to]});
    transition.events.push_back(puzzle_->bindings.events[a.event]);
  }
  return {std::move(transition), {}};
}
ExecutionPlan Execution::inverse(const CompletedTransition &transition) const {
  const auto &op = definition_->operations.at(definition_->operation_ids.at(transition.operation));
  if (!relational())
    return plan(transition.after, op.inverse);
  Json choices = Json::array();
  for (Index d = 0; d < transition.choices.size(); ++d)
    choices.push_back(inverse_moves_[d].at(transition.choices[d]));
  return plan(transition.after, op.inverse, choices);
}
Json Execution::encode(const CompletedTransition &transition) const {
  auto wire = encode_transition(*definition_, transition);
  if (relational()) {
    wire["witnessVersion"] = 1;
    wire["request"]["choices"] = transition.choices;
    for (Index i = 0; i < transition.events.size(); ++i)
      wire["pieceActions"][i]["event"] = transition.events[i];
  }
  return wire;
}
Json Execution::legal(const State &state) const {
  if (!relational())
    return legal_operations(*definition_, state);
  auto requests = Json::array();
  for (const auto &r : puzzle_->bindings.requests) {
    const auto candidate = plan(state, r.id);
    if (candidate.transition)
      requests.push_back(
          {{"operation", r.id}, {"parameters", Json::object()}, {"choices", candidate.transition->choices}});
  }
  return requests;
}
} // namespace twisty
