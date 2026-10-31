#include "twisty/execution.hpp"
#include "twisty/controller.hpp"
#include "../json_input.hpp"
#include "twisty/authoring/gear.hpp"
#include <algorithm>
#include <set>

namespace twisty {
// Compatibility source adapter. Runtime route resolution uses compiled bindings.
std::shared_ptr<const ControlCatalog> ControlCatalog::compile(const Definition &definition,
                                                              const Json &source, const Json &controls) {
  const auto puzzle = language::compile(source);
  return compile(definition, puzzle.bindings, controls);
}
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
    if ((document.at("formatVersion") != 1 && document.at("formatVersion") != 2 && document.at("formatVersion") != 3))
      throw DiagnosticError("schema.unsupported", "/formatVersion",
                            "Expected relational format version 1, 2 or 3.");
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
  std::map<std::string, Index> binding_requests;
  for (Index r = 0; r < bindings.requests.size(); ++r) binding_requests.emplace(bindings.requests[r].id, r);
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
    } else {
      for (const auto &[op, metadata] : catalog.at("requests").items()) {
        const auto &r = bindings.requests.at(binding_requests.at(op));
        const auto &inverse = bindings.requests.at(binding_requests.at(metadata.at("inverse").get<std::string>()));
        ensure(r.default_profile && inverse.default_profile, "Inverse binding requires concrete default profiles.");
        const auto choice = r.profiles[*r.default_profile].choices[d], reversed = inverse.profiles[*inverse.default_profile].choices[d];
        const auto &value = bindings.choice_values[d][choice], &target = bindings.choice_values[d][reversed];
        const auto [it, added] = inverses.emplace(value, target);
        ensure(added || it->second == target, "Requests disagree on a local inverse binding.");
      }
      for (const auto &value : bindings.choice_values[d])
        ensure(inverses.contains(value) && inverses.contains(inverses.at(value)) && inverses.at(inverses.at(value)) == value,
          "Local inverse bindings must be complete and reciprocal.");
    }
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
  if (catalog.contains("controls"))
    controls_ = ControlCatalog::compile(*definition, bindings, catalog.at("controls"));
  definition->canonical["source"] = std::move(source);
  definition->canonical["catalog"] = std::move(catalog);
  // Versioned canonical bytes cover the executable, completion policy, symbolic
  // mapping, inverse metadata, and port bindings. Old semantic pins cannot match.
  definition->digest = sha256(definition->canonical.dump());
  definition_ = std::move(definition);
}
} // namespace twisty
