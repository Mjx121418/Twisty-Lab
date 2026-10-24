#include "twisty/execution.hpp"
#include "json_input.hpp"
#include "twisty/authoring/gear.hpp"
#include <algorithm>
#include <set>

namespace twisty {
Json compile_execution_json(const std::string &text) {
  try {
    return {{"status", "Compiled"},
            {"definition", Execution::from_source(text)->definition_json()},
            {"diagnostics", Json::array()}};
  } catch (const core::Error &error) {
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

std::shared_ptr<const Execution> Execution::from_source(const std::string &text) {
  if (text.size() > 16 * 1024 * 1024)
    throw DiagnosticError("document.limit", "/", "Execution documents are limited to 16 MiB.");
  detail::check_json_depth(text, 512, [] {
    throw DiagnosticError("document.depth", "/", "Execution document nesting exceeds 512 levels.");
  });
  auto document = Json::parse(text);
  const auto kind = document.value("kind", std::string{});
  if (kind == "relational-gear-prototypes") {
    auto module = language::gear_module_source(document);
    return std::shared_ptr<const Execution>(
        new Execution(std::move(module.source), std::move(module.catalog)));
  }
  if (kind == "relational-package" || kind == "relational-definition") {
    if ((document.at("formatVersion") != 1 && document.at("formatVersion") != 2))
      throw DiagnosticError("schema.unsupported", "/formatVersion",
                            "Expected relational format version 1 or 2.");
    if (document.at("source").at("languageVersion") != document.at("formatVersion"))
      throw DiagnosticError("schema.unsupported", "/source/languageVersion",
                            "Package/definition and source versions differ.");
    // Transfer the large AST/catalog into the canonical definition. Their
    // equality is guaranteed by construction; verify every other stored field
    // after recompiling without keeping a second AST alive.
    const auto fields = document.size();
    auto source = std::move(document.at("source")), catalog = std::move(document.at("catalog"));
    auto execution = std::shared_ptr<const Execution>(new Execution(std::move(source), std::move(catalog)));
    if (kind == "relational-definition") {
      const auto &expected = execution->definition_->canonical;
      const bool equal =
          fields == expected.size() + 1 &&
          document.value("definitionDigest", std::string{}) == execution->definition_->digest &&
          std::all_of(expected.items().begin(), expected.items().end(), [&](const auto &item) {
            if (item.key() == "source" || item.key() == "catalog")
              return true;
            return document.contains(item.key()) && document.at(item.key()) == item.value();
          });
      if (!equal)
        throw DiagnosticError("definition.digest_mismatch", "/",
                              "Relational definition differs from its compiled contents.");
    }
    return execution;
  }
  throw DiagnosticError(
      "definition.unsupported", "/kind",
      "Expected a relational-package, relational-definition, or relational-gear-prototypes. "
      "Legacy definitions and prototype sources are no longer supported.");
}

Execution::Execution(Json source, Json catalog)
    : puzzle_(std::make_shared<language::CompiledPuzzle>(language::compile(source))) {
  // Grouped tables below are frame/inspector metadata. State validity and move
  // permission always come from the separately compiled relational program.
  const bool port_catalog = catalog.at("kind") == "relational-port-bindings";
  articulated_events_ = catalog.at("kind") == "relational-articulated-bindings";
  ensure((port_catalog || articulated_events_ || catalog.at("kind") == "relational-cube-bindings") &&
             catalog.at("version") == 1,
         "Expected a version-1 cube, port, or articulated binding catalog.");
  const auto &bindings = puzzle_->bindings;
  ensure(catalog.at("puzzleId").get<std::string>() == bindings.puzzle_id, "Puzzle IDs differ.");
  auto definition = std::make_shared<Definition>();
  definition->id = bindings.puzzle_id;
  definition->port_catalog = port_catalog || articulated_events_;
  definition->cells = catalog.at("resources").get<std::vector<std::string>>();
  const auto indexed = [](const std::vector<std::string> &names) {
    std::map<std::string, Index> result;
    for (Index i = 0; i < names.size(); ++i)
      ensure(result.emplace(names[i], i).second, "Duplicate binding symbol.");
    return result;
  };
  const auto placement_ids = indexed(bindings.placements), resource_ids = indexed(definition->cells);
  const auto lookup = [](const auto &table, const std::string &name) {
    const auto found = table.find(name);
    ensure(found != table.end(), "Binding references an unknown symbol.");
    return found->second;
  };
  ensure(catalog.at("domains").size() == source.at("domains").size(), "Domain counts differ.");
  local_of_global_.resize(bindings.placements.size());
  inverse_moves_.resize(bindings.choice_slots.size());
  Json types = Json::array(), domains = Json::array(), pieces = Json::array(), operations = Json::array();
  std::map<std::string, Index> domain_ids;
  std::vector<std::map<std::string, const Json *>> domain_moves(source.at("domains").size());
  std::set<std::string> declared_placements, declared_pieces;
  for (Index d = 0; d < source.at("domains").size(); ++d) {
    const auto &s = source.at("domains").at(d), &c = catalog.at("domains").at(d);
    const auto id = s.at("id").get<std::string>();
    ensure(c.at("id").get<std::string>() == id, "Domain order differs.");
    domain_ids.emplace(id, d);
    for (const auto &move : s.at("moves"))
      domain_moves[d].emplace(move.at("id").get<std::string>(), &move);
    Domain domain;
    domain.id = id;
    Json wire = Json::array();
    std::vector<Index> global;
    for (const auto &q : s.at("placements")) {
      const auto key = q.get<std::string>(), qualified = id + "/" + key;
      ensure(declared_placements.insert(qualified).second, "Duplicate placement binding.");
      const auto g = lookup(placement_ids, qualified);
      local_of_global_[g] = static_cast<Index>(global.size());
      global.push_back(g);
      const auto &entry = catalog.at("placements").at(qualified);
      Placement placement;
      placement.key = key;
      placement.footprint = entry.at("resources").get<std::vector<std::string>>();
      for (const auto &cell : placement.footprint)
        (void)lookup(resource_ids, cell);
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
    if (articulated_events_) {
      const auto &declared = catalog.at("moveInverses").at(id);
      ensure(declared.size() == s.at("moves").size(), "Each local move needs an inverse binding.");
      for (const auto &move : s.at("moves")) {
        const auto name = move.at("id").get<std::string>();
        const auto inverse = declared.at(name).get<std::string>();
        ensure(declared.contains(inverse) && declared.at(inverse).get<std::string>() == name,
               "Local inverse bindings must be reciprocal.");
        inverses[name] = inverse;
      }
    } else
      for (const auto &[op, metadata] : catalog.at("requests").items())
        inverses[op] = metadata.at("inverse").get<std::string>();
    std::vector<std::string> expected_pieces;
    for (const auto &p : s.at("pieces"))
      expected_pieces.push_back(p.at("id").get<std::string>());
    ensure(c.at("pieces").get<std::vector<std::string>>() == expected_pieces, "Piece binding order differs.");
  }
  ensure(declared_placements.size() == catalog.at("placements").size(), "Extraneous placement bindings.");
  // The presentation tables are sorted by symbolic piece ID. Flat indices
  // are independently mapped, never inferred from that ordering.
  auto sorted_pieces = bindings.pieces;
  std::sort(sorted_pieces.begin(), sorted_pieces.end());
  display_piece_of_core_.resize(sorted_pieces.size());
  for (const auto &id : sorted_pieces) {
    const auto p = index_of(bindings.pieces, id);
    core_piece_of_display_.push_back(p);
    display_piece_of_core_[p] = static_cast<Index>(definition->pieces.size());
    const auto &entry = catalog.at("pieces").at(id);
    const auto d = domain_ids.at(entry.at("domain").get<std::string>());
    const auto home = lookup(placement_ids, entry.at("home").get<std::string>());
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
  auto program_view = std::shared_ptr<const core::Program>(puzzle_, &puzzle_->program);
  auto local_view = std::make_shared<const std::vector<Index>>(local_of_global_);
  std::vector<std::shared_ptr<const std::vector<Index>>> placement_views;
  for (const auto &v : global_of_local_)
    placement_views.push_back(std::make_shared<const std::vector<Index>>(v));
  for (Index r = 0; r < bindings.requests.size(); ++r) {
    const auto &request = bindings.requests[r];
    request_ids_.emplace(request.id, r);
    const auto &c = catalog.at("requests").at(request.id);
    ensure(request.default_profile.has_value(),
           "A uniform rotation track requires a declared default profile.");
    const auto &profile = request.profiles.at(*request.default_profile);
    ensure(port_catalog || articulated_events_ || profile.when == core::true_ref,
           "The cube frame adapter requires a constant declared default profile.");
    const auto &choices = profile.choices;
    Operation operation;
    operation.id = request.id;
    operation.inverse = c.at("inverse").get<std::string>();
    operation.family = c.at("family").get<std::string>();
    operation.transport = c.at("event").get<std::string>();
    ensure(profile.id == c.at("profile").get<std::string>(), "Profile binding differs.");
    if (!port_catalog && !articulated_events_)
      for (const auto &cell : definition->cells)
        if (cell.find(operation.family) != std::string::npos)
          operation.cells.push_back(cell);
    const auto unavailable = c.value("unavailablePlacements", std::vector<std::string>{});
    const std::set<std::string> unavailable_set(unavailable.begin(), unavailable.end());
    for (const auto &q : unavailable)
      lookup(placement_ids, q);
    for (Index d = 0; d < source.at("domains").size(); ++d) {
      const auto movement = bindings.choice_values[d].at(choices.at(d));
      ensure(articulated_events_ || movement == request.id,
             "Uniform track requires the same named move in every domain.");
      const auto found = domain_moves[d].find(movement);
      ensure(found != domain_moves[d].end(), "Unknown movement binding.");
      const auto *move = found->second;
      ensure(articulated_events_ || move->at("event").get<std::string>() == operation.transport,
             "Event binding differs from the movement.");
      if (!articulated_events_)
        event_operations_[{d, operation.transport}] = r;
      auto view = std::make_shared<OperationTransport>();
      view->program = program_view;
      view->placements = placement_views[d];
      view->local = local_view;
      view->map = bindings.move_maps[d].at(choices.at(d));
      const auto &domain = definition->domains[d];
      for (Index q = 0; q < domain.placements.size(); ++q) {
        const auto image = puzzle_->program.transport(view->map, global_of_local_[d][q]);
        const bool excluded = unavailable_set.contains(domain.id + "/" + domain.placements[q].key);
        ensure(!image || std::find(global_of_local_[d].begin(), global_of_local_[d].end(), *image) !=
                             global_of_local_[d].end(),
               "Movement leaves its authoring domain.");
        ensure(!excluded || !image, "An unavailable frame section cannot participate.");
        if (excluded)
          view->unavailable.push_back(q);
      }
      operation.maps.push_back({view});
      operation.roles.push_back({view});
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
  if (articulated_events_) {
    std::set<std::string> expected;
    for (Index d = 0; d < source.at("domains").size(); ++d)
      for (const auto &move : source.at("domains").at(d).at("moves")) {
        const auto event = move.at("event").get<std::string>();
        const auto &binding = catalog.at("events").at(event);
        ensure(binding.at("domain").get<std::string>() == definition->domains[d].id,
               "An event is bound to a different authoring domain.");
        const auto name = binding.at("request").get<std::string>();
        ensure(request_ids_.contains(name), "Event track references an unknown request.");
        const auto r = request_ids_.at(name);
        const auto &request = bindings.requests[r];
        const auto choice = request.profiles.at(*request.default_profile).choices[d];
        ensure(bindings.choice_values[d][choice] == move.at("id").get<std::string>(),
               "Event track must certify exactly its local movement.");
        event_operations_[{d, event}] = r;
        expected.insert(event);
      }
    ensure(expected.size() == catalog.at("events").size(), "Extraneous event bindings.");
  }
  auto preview = language::inspect(*puzzle_);
  // Inspection metrics are not execution semantics or part of a definition pin.
  preview["program"].erase("transportStorageBytes");
  definition->canonical = {{"kind", "relational-definition"}, {"formatVersion", puzzle_->source_version},
                           {"predicateVersion", 1},           {"puzzleId", definition->id},
                           {"ruleModule", "relational@1"},    {"cells", definition->cells},
                           {"pieceTypes", Json::array()}};
  definition->canonical["pieceTypes"] = std::move(types);
  definition->canonical["placementDomains"] = std::move(domains);
  definition->canonical["pieces"] = std::move(pieces);
  definition->canonical["operations"] = std::move(operations);
  definition->canonical["program"] = std::move(preview.at("program"));
  definition->canonical["bindings"] = std::move(preview.at("bindings"));
  if (catalog.contains("symmetry"))
    definition->canonical["symmetry"] = {{"members", catalog.at("symmetry")}};
  definition->canonical["source"] = std::move(source);
  definition->canonical["catalog"] = std::move(catalog);
  // Versioned canonical bytes cover the executable, completion policy, symbolic
  // mapping, inverse metadata, and port bindings. Old semantic pins cannot match.
  definition->digest = sha256(definition->canonical.dump());
  definition_ = std::move(definition);
}
Json Execution::definition_json() const {
  auto result = definition_->canonical;
  result["definitionDigest"] = definition_->digest;
  return result;
}
Index Execution::event_operation(Index piece, const std::string &event) const {
  ensure(piece < definition_->pieces.size(), "Unknown event participant.");
  const auto found = event_operations_.find({definition_->pieces[piece].domain, event});
  ensure(found != event_operations_.end(), "Unknown or incompatible participation event.");
  return found->second;
}
core::State Execution::flatten(const State &state) const {
  if (state.placement_of.size() != definition_->pieces.size() || !state.mechanism.is_object() ||
      !state.mechanism.empty())
    throw DiagnosticError("state.invalid", "/state",
                          "Expected exactly the declared pieces and an empty mechanism record.");
  core::State result(state.placement_of.size());
  for (Index p = 0; p < result.size(); ++p) {
    const auto &map = global_of_local_.at(definition_->pieces[p].domain);
    if (state.placement_of[p] >= map.size())
      throw DiagnosticError("state.placement", "/placementOf", "Placement is outside the declared catalog.");
    result[core_piece_of_display_[p]] = map[state.placement_of[p]];
  }
  return result;
}
State Execution::display(const core::State &state) const {
  State result;
  result.placement_of.resize(state.size());
  for (Index p = 0; p < state.size(); ++p)
    result.placement_of[display_piece_of_core_[p]] = local_of_global_.at(state[p]);
  return result;
}
void Execution::validate(const State &state) const {
  if (!puzzle_->program.valid(flatten(state)))
    throw DiagnosticError("state.invariant", "/state", "State violates the compiled invariant.");
}
State Execution::decode(const Json &wire) const {
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
  return puzzle_->program.solved(flatten(state));
}
ExecutionPlan Execution::plan(const State &state, const std::string &operation,
                              const Json &named_choices) const {
  validate(state);
  if (!request_ids_.contains(operation))
    throw DiagnosticError("request.unknown", "/operation", "Unknown request.");
  const auto request = request_ids_.at(operation);
  const auto before = flatten(state);
  std::optional<core::Choices> choices;
  if (named_choices.is_null())
    choices = language::bind_profile(*puzzle_, request, before);
  else {
    const auto names = named_choices.get<std::vector<std::string>>();
    if (names.size() != puzzle_->bindings.choice_slots.size())
      throw DiagnosticError("request.choices", "/choices",
                            "Supply one choice for every declared choice slot.");
    choices = core::Choices{};
    for (Index i = 0; i < names.size(); ++i)
      choices->push_back(index_of(puzzle_->bindings.choice_values[i], names[i]));
  }
  if (!choices)
    return {std::nullopt, {{"status", "ChoiceRequired"}, {"operation", operation}}};
  const auto result = puzzle_->program.plan(request, before, *choices);
  if (!result.transition) {
    const auto reason = result.rejection == core::Rejection::relation ? "relation.rejected"
                        : result.rejection == core::Rejection::target_invariant
                            ? "state.target_invariant"
                            : "transport.conflicting_writes";
    std::set<std::string> inputs;
    const auto root = puzzle_->program.data().requests[request].relation;
    if (result.rejection == core::Rejection::relation)
      for (const auto p : language::rejection_inputs(puzzle_->program, request, before, *choices))
        inputs.insert(puzzle_->bindings.pieces[p]);
    return {std::nullopt,
            {{"status", "Blocked"},
             {"reasonCode", reason},
             {"operation", operation},
             {"scope", "completed-request"},
             {"implicatedPieces", inputs},
             {"evidence", {{"kind", "relation-proof-inputs"}, {"relationRoot", root}}}}};
  }
  const auto &w = *result.transition;
  CompletedTransition transition{{operation, state, display(w.after), {}}, {}, {}};
  for (Index i = 0; i < choices->size(); ++i)
    transition.choices.push_back(puzzle_->bindings.choice_values[i][choices->at(i)]);
  for (const auto &a : w.actions) {
    transition.actions.push_back(
        {display_piece_of_core_[a.piece], local_of_global_[a.from], local_of_global_[a.to]});
    transition.events.push_back(puzzle_->bindings.events[a.event]);
  }
  return {std::move(transition), {}};
}
ExecutionPlan Execution::inverse(const CompletedTransition &transition) const {
  const auto &op = definition_->operations.at(definition_->operation_ids.at(transition.operation));
  Json choices = Json::array();
  for (Index d = 0; d < transition.choices.size(); ++d)
    choices.push_back(inverse_moves_[d].at(transition.choices[d]));
  return plan(transition.after, op.inverse, choices);
}
Json Execution::encode(const CompletedTransition &transition) const {
  auto wire = encode_transition(*definition_, transition);
  wire["witnessVersion"] = 1;
  wire["request"]["choices"] = transition.choices;
  for (Index i = 0; i < transition.events.size(); ++i)
    wire["pieceActions"][i]["event"] = transition.events[i];
  return wire;
}
Json Execution::legal(const State &state) const {
  auto requests = Json::array();
  const auto before = flatten(state);
  const auto candidates = language::bind_default_profiles(*puzzle_, before);
  for (Index r = 0; r < candidates.size(); ++r) {
    if (candidates[r] && puzzle_->program.plan(r, before, *candidates[r]).transition) {
      auto choices = Json::array();
      for (Index d = 0; d < candidates[r]->size(); ++d)
        choices.push_back(puzzle_->bindings.choice_values[d][candidates[r]->at(d)]);
      requests.push_back({{"operation", puzzle_->bindings.requests[r].id},
                          {"parameters", Json::object()},
                          {"choices", choices}});
    }
  }
  return requests;
}
Json Execution::candidates(const State &state) const {
  const auto source = flatten(state);
  const auto candidates = language::bind_default_profiles(*puzzle_, source);
  auto requests = Json::array();
  for (Index r = 0; r < puzzle_->bindings.requests.size(); ++r)
    if (const auto &tuple = candidates[r]) {
      auto choices = Json::array();
      for (Index d = 0; d < tuple->size(); ++d)
        choices.push_back(puzzle_->bindings.choice_values[d][tuple->at(d)]);
      requests.push_back({{"operation", puzzle_->bindings.requests[r].id},
                          {"parameters", Json::object()},
                          {"choices", choices}});
    }
  return requests;
}
} // namespace twisty
