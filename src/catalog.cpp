#include "twisty/catalog.hpp"

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
Json encode_state(const Definition &definition, const State &state) {
  Json placement = Json::object();
  for (Index i = 0; i < definition.pieces.size(); ++i) {
    const auto &piece = definition.pieces[i];
    placement[piece.id] = definition.domains[piece.domain].placements.at(state.placement_of.at(i)).key;
  }
  return {{"placementOf", placement}, {"mechanism", state.mechanism}};
}
std::string state_digest(const Definition &definition, const State &state) {
  return sha256(
      Json{{"definitionDigest", definition.digest}, {"state", encode_state(definition, state)}}.dump());
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
} // namespace twisty
