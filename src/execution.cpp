#include "twisty/execution.hpp"
#include "twisty/controller.hpp"
#include "twisty/artifact.hpp"
#include <algorithm>
#include <set>
#include <numeric>

namespace twisty {
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
Json Execution::definition_json() const {
  if (source_free()) return artifact_->document();
  auto result = definition_->canonical;
  result["definitionDigest"] = definition_->digest;
  return result;
}
std::shared_ptr<const Execution> Execution::from_artifact(const std::string &text) {
  return std::shared_ptr<const Execution>(new Execution(std::make_shared<artifact::Artifact>(text)));
}
Execution::Execution(std::shared_ptr<const artifact::Artifact> artifact)
    : artifact_(std::move(artifact)), puzzle_(artifact_->compiled()) {
  if (!artifact_->has_session()) throw DiagnosticError("artifact.capability", "/session", "Session loading requires bindings and inverse tables.");
  const auto &b = puzzle_->bindings;
  auto definition = std::make_shared<Definition>();
  definition->source_free = true;
  definition->id = b.puzzle_id;
  definition->digest = artifact_->document().at("executionDigest");
  definition->canonical = {{"kind", "twisty-abstract-artifact"}};
  Domain domain; domain.id = "placements";
  for (Index q = 0; q < b.placements.size(); ++q) {
    domain.by_key.emplace(b.placements[q], q);
    domain.placements.push_back({b.placements[q], {}, {}});
  }
  definition->domains.push_back(std::move(domain));
  global_of_local_.resize(1);
  global_of_local_[0].resize(b.placements.size());
  std::iota(global_of_local_[0].begin(), global_of_local_[0].end(), 0);
  local_of_global_ = global_of_local_[0];
  for (Index p = 0; p < b.pieces.size(); ++p) {
    const auto home = puzzle_->program.data().initial[p];
    definition->pieces.push_back({b.pieces[p], "placements", 0, home, {}});
    definition->initial.placement_of.push_back(home);
    core_piece_of_display_.push_back(p); display_piece_of_core_.push_back(p);
  }
  for (Index r = 0; r < b.requests.size(); ++r) {
    Operation op;
    op.id = b.requests[r].id;
    op.inverse = b.requests[artifact_->request_inverses()[r]].id;
    op.transport = op.id; // Opaque diagnostic identity; geometry never parses it.
    request_ids_.emplace(op.id, r); definition->operation_ids.emplace(op.id, r);
    definition->operations.push_back(std::move(op));
  }
  inverse_moves_.resize(b.choice_values.size());
  for (Index slot = 0; slot < b.choice_values.size(); ++slot)
    for (Index value = 0; value < b.choice_values[slot].size(); ++value)
      inverse_moves_[slot].emplace(b.choice_values[slot][value], b.choice_values[slot][artifact_->choice_inverses()[slot][value]]);
  definition_ = std::move(definition);
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
std::vector<std::string> Execution::participating_moves(const State &state, Index piece) const {
  const auto domain = definition_->pieces.at(piece).domain;
  const auto placement = global_of_local_.at(domain).at(state.placement_of.at(piece));
  const auto &values = puzzle_->bindings.choice_values.at(domain);
  std::vector<std::string> moves;
  for (Index choice = 1; choice < values.size(); ++choice)
    if (runtime::movement_participates(*puzzle_, domain, choice, core_piece_of_display_.at(piece), placement))
      moves.push_back(values[choice]);
  return moves;
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
    choices = runtime::bind_profile(*puzzle_, request, before);
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
      for (const auto p : runtime::rejection_inputs(puzzle_->program, request, before, *choices))
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
  wire["witnessVersion"] = source_free() ? 2 : 1;
  wire["request"]["choices"] = transition.choices;
  for (Index i = 0; i < transition.events.size(); ++i)
    wire["pieceActions"][i]["event"] = transition.events[i];
  return wire;
}
Json Execution::legal(const State &state) const {
  auto requests = Json::array();
  const auto before = flatten(state);
  const auto candidates = runtime::bind_default_profiles(*puzzle_, before);
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
  const auto candidates = runtime::bind_default_profiles(*puzzle_, source);
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
