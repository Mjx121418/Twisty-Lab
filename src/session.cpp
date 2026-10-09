#include "twisty/session.hpp"
#include <algorithm>
#include <limits>

namespace twisty {
namespace {
template <class F> std::string guarded(F function) {
  try {
    auto result = function();
    if (result.contains("transitions")) {
      result["transitionRecords"] = Json::array();
      for (const auto &transition : result.at("transitions"))
        result["transitionRecords"].push_back(transition.dump());
    }
    return result.dump();
  } catch (const std::exception &error) {
    return diagnostic_result(error).dump();
  }
}
Json stale_result(const std::string &revision) {
  return {{"status", "StaleRevision"}, {"revision", revision}};
}
std::uint64_t parse_revision(const std::string &text) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
    throw DiagnosticError("revision.invalid", "/revision", "Revision must be a decimal integer string.");
  std::size_t consumed = 0;
  const auto value = std::stoull(text, &consumed);
  if (consumed != text.size() || value == std::numeric_limits<std::uint64_t>::max())
    throw DiagnosticError("revision.invalid", "/revision", "Revision is outside the supported range.");
  return value;
}
} // namespace
Session::Session(const std::string &source) {
  auto document = Json::parse(source);
  if (document.value("kind", std::string{}) == "finite-definition")
    definition_ = load_definition(std::move(document));
  else {
    const auto result = compile_json(source);
    if (result.at("status") != "Compiled") {
      const auto &diagnostic = result.at("diagnostics").at(0);
      throw DiagnosticError(diagnostic.at("reasonCode"), diagnostic.value("source", "/"),
                            diagnostic.at("message"));
    }
    definition_ = load_definition(result.at("definition"));
  }
  origin_ = definition_->initial;
  state_ = origin_;
}
std::string Session::revision() const {
  return std::to_string(revision_);
}
bool Session::stale(const std::string &expected) const {
  return parse_revision(expected) != revision_;
}
Json Session::snapshot() const {
  if (cached_snapshot_ && cached_revision_ == revision_)
    return *cached_snapshot_;
  Json history = Json::array();
  for (const auto &transition : history_)
    history.push_back(transition.operation);
  cached_snapshot_ = Json{{"definitionDigest", definition_->digest},
                          {"puzzleId", definition_->id},
                          {"revision", revision()},
                          {"stateDigest", state_digest(*definition_, state_)},
                          {"state", encode_state(*definition_, state_)},
                          {"solved", solved(*definition_, state_)},
                          {"legalRequests", legal_operations(*definition_, state_)},
                          {"history", history},
                          {"cursor", cursor_},
                          {"canUndo", cursor_ > 0},
                          {"canRedo", cursor_ < history_.size()}};
  cached_revision_ = revision_;
  return *cached_snapshot_;
}
Json Session::commit(const Transition &transition, bool truncate) {
  if (truncate && cursor_ < history_.size())
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
  state_ = transition.after;
  history_.push_back(transition);
  ++cursor_;
  ++revision_;
  return encode_transition(*definition_, transition);
}
Json Session::execute(const std::string &operation, const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  const auto result = plan(*definition_, state_, operation);
  if (!result.transition)
    return result.blocked;
  const auto transition = commit(*result.transition);
  return {{"status", "Committed"}, {"transitions", Json::array({transition})}, {"snapshot", snapshot()}};
}
Json Session::run(const std::string &notation, const std::string &policy, const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  if (policy != "interactive" && policy != "transactional")
    throw DiagnosticError("sequence.policy", "/policy", "Policy must be interactive or transactional.");
  const auto moves = parse_notation(*definition_, notation);
  Json transitions = Json::array();
  std::vector<Transition> staged;
  auto temporary = state_;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    auto result = plan(*definition_, policy == "transactional" ? temporary : state_, moves[i]);
    if (!result.transition) {
      auto blocked = result.blocked;
      blocked["failureIndex"] = i;
      blocked["failureState"] = encode_state(*definition_, policy == "transactional" ? temporary : state_);
      blocked["committed"] = transitions.size();
      blocked["transitions"] = transitions;
      blocked["snapshot"] = snapshot();
      return blocked;
    }
    if (policy == "transactional") {
      temporary = result.transition->after;
      staged.push_back(std::move(*result.transition));
    } else
      transitions.push_back(commit(*result.transition));
  }
  for (const auto &transition : staged)
    transitions.push_back(commit(transition));
  return {{"status", "Committed"}, {"transitions", transitions}, {"snapshot", snapshot()}};
}
Json Session::undo(const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  if (cursor_ == 0)
    return {{"status", "NoHistory"}};
  const auto &previous = history_[cursor_ - 1];
  const auto &operation = definition_->operations.at(definition_->operation_ids.at(previous.operation));
  auto result = plan(*definition_, state_, operation.inverse);
  if (!result.transition || result.transition->after != previous.before)
    throw DiagnosticError("history.inverse", "/history", "Inverse did not restore the full recorded state.");
  state_ = result.transition->after;
  --cursor_;
  ++revision_;
  return {{"status", "Committed"},
          {"transitions", Json::array({encode_transition(*definition_, *result.transition)})},
          {"snapshot", snapshot()}};
}
Json Session::redo(const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  if (cursor_ == history_.size())
    return {{"status", "NoHistory"}};
  auto result = plan(*definition_, state_, history_[cursor_].operation);
  if (!result.transition || result.transition->after != history_[cursor_].after)
    throw DiagnosticError("history.replay", "/history", "Redo differs from the recorded transition.");
  state_ = result.transition->after;
  ++cursor_;
  ++revision_;
  return {{"status", "Committed"},
          {"transitions", Json::array({encode_transition(*definition_, *result.transition)})},
          {"snapshot", snapshot()}};
}
Json Session::scramble(std::uint32_t seed, std::uint32_t length, const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  if (length > 1000)
    throw DiagnosticError("scramble.limit", "/length", "Scrambles are limited to 1000 primitives.");
  auto random = seed == 0 ? std::uint32_t{0x6d2b79f5} : seed;
  auto next = [&]() {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  auto temporary = state_;
  std::vector<std::string> moves;
  std::vector<Transition> staged;
  std::string excluded;
  for (std::uint32_t i = 0; i < length; ++i) {
    std::vector<std::string> candidates;
    for (const auto &request : legal_operations(*definition_, temporary))
      candidates.push_back(request.at("operation").get<std::string>());
    if (candidates.empty())
      break;
    if (candidates.size() > 1)
      std::erase(candidates, excluded);
    const auto bound = static_cast<std::uint32_t>(candidates.size());
    const auto threshold = static_cast<std::uint32_t>(-bound) % bound;
    auto draw = next();
    while (draw < threshold)
      draw = next();
    const auto selected = candidates[draw % bound];
    moves.push_back(selected);
    auto transition = *plan(*definition_, temporary, selected).transition;
    temporary = transition.after;
    staged.push_back(std::move(transition));
    excluded = definition_->operations.at(definition_->operation_ids.at(selected)).inverse;
  }
  std::string notation;
  for (const auto &move : moves) {
    if (!notation.empty())
      notation += ' ';
    notation += move;
  }
  Json transitions = Json::array();
  for (const auto &transition : staged)
    transitions.push_back(commit(transition));
  Json result{{"status", "Committed"}, {"transitions", transitions}, {"snapshot", snapshot()}};
  result["scramble"] = {{"seed", seed},
                        {"generatorVersion", "xorshift32-v1"},
                        {"selectionPolicy", "legal-walk/no-immediate-inverse-v1"},
                        {"requests", moves},
                        {"notation", notation},
                        {"termination", moves.size() == length ? "complete" : "no-legal-continuation"}};
  return result;
}
Json Session::save() const {
  Json requests = Json::array();
  Json checkpoints = Json::array({{{"index", 0},
                                   {"state", encode_state(*definition_, origin_)},
                                   {"stateDigest", state_digest(*definition_, origin_)}}});
  for (std::size_t i = 0; i < history_.size(); ++i) {
    requests.push_back({{"operation", history_[i].operation},
                        {"parameters", Json::object()},
                        {"afterStateDigest", state_digest(*definition_, history_[i].after)}});
    if ((i + 1) % 100 == 0)
      checkpoints.push_back({{"index", i + 1},
                             {"state", encode_state(*definition_, history_[i].after)},
                             {"stateDigest", state_digest(*definition_, history_[i].after)}});
  }
  return {{"schemaVersion", 1},
          {"kind", "session"},
          {"logical",
           {{"definitionDigest", definition_->digest},
            {"originState", encode_state(*definition_, origin_)},
            {"requests", requests},
            {"cursor", cursor_},
            {"revision", revision()},
            {"stateDigest", state_digest(*definition_, state_)},
            {"checkpoints", checkpoints}}},
          {"presentation", Json::object()}};
}
Json Session::load(const Json &document) {
  if (document.at("schemaVersion") != 1 || document.at("kind") != "session")
    throw DiagnosticError("session.schema", "/", "Expected a schema-v1 session.");
  const auto &logical = document.at("logical");
  if (logical.at("definitionDigest") != definition_->digest)
    throw DiagnosticError("definition.digest_mismatch", "/logical/definitionDigest",
                          "Saved session uses different puzzle semantics.");
  const auto origin = decode_state(*definition_, logical.at("originState"));
  auto temporary = origin;
  std::vector<Transition> history;
  if (logical.at("requests").size() > 100000)
    throw DiagnosticError("history.limit", "/logical/requests", "History exceeds 100000 requests.");
  for (const auto &request : logical.at("requests")) {
    if (!request.at("parameters").empty())
      throw DiagnosticError("request.parameters", "/logical/requests",
                            "These primitives do not accept parameters.");
    auto result = plan(*definition_, temporary, request.at("operation"));
    if (!result.transition)
      throw DiagnosticError("history.blocked", "/logical/requests", result.blocked.dump());
    if (request.at("afterStateDigest") != state_digest(*definition_, result.transition->after))
      throw DiagnosticError("history.digest", "/logical/requests",
                            "Replay state differs from its recorded digest.");
    temporary = result.transition->after;
    history.push_back(std::move(*result.transition));
  }
  const auto cursor = logical.at("cursor").get<std::size_t>();
  if (cursor > history.size())
    throw DiagnosticError("history.cursor", "/logical/cursor", "History cursor is out of range.");
  temporary = cursor == 0 ? origin : history[cursor - 1].after;
  if (logical.at("stateDigest") != state_digest(*definition_, temporary))
    throw DiagnosticError("history.digest", "/logical/stateDigest",
                          "Saved state digest does not match its replay.");
  for (const auto &checkpoint : logical.at("checkpoints")) {
    const auto index = checkpoint.at("index").get<std::size_t>();
    if (index > history.size())
      throw DiagnosticError("history.checkpoint", "/logical/checkpoints",
                            "Checkpoint index is outside history.");
    const auto expected_state = index == 0 ? origin : history[index - 1].after;
    if (decode_state(*definition_, checkpoint.at("state")) != expected_state ||
        checkpoint.at("stateDigest") != state_digest(*definition_, expected_state))
      throw DiagnosticError("history.checkpoint", "/logical/checkpoints", "Checkpoint differs from replay.");
  }
  const auto loaded_revision = parse_revision(logical.at("revision").get<std::string>());
  origin_ = origin;
  state_ = temporary;
  history_ = std::move(history);
  cursor_ = cursor;
  revision_ = std::max(revision_, loaded_revision) + 1;
  return {{"status", "Loaded"}, {"snapshot", snapshot()}};
}
std::string Session::definition_json() const {
  auto output = definition_->canonical;
  output["definitionDigest"] = definition_->digest;
  output["provenance"] = definition_->provenance;
  return output.dump();
}
std::string Session::execute_json(const std::string &request, const std::string &expected) {
  return guarded([&]() {
    const auto parsed = Json::parse(request);
    if (!parsed.at("parameters").is_object() || !parsed.at("parameters").empty())
      throw DiagnosticError("request.parameters", "/parameters",
                            "These primitives accept an empty parameter record.");
    return execute(parsed.at("operation"), expected);
  });
}
std::string Session::run_json(const std::string &notation, const std::string &policy,
                              const std::string &expected) {
  return guarded([&]() { return run(notation, policy, expected); });
}
std::string Session::undo_json(const std::string &expected) {
  return guarded([&]() { return undo(expected); });
}
std::string Session::redo_json(const std::string &expected) {
  return guarded([&]() { return redo(expected); });
}
std::string Session::scramble_json(std::uint32_t seed, std::uint32_t length, const std::string &expected) {
  return guarded([&]() { return scramble(seed, length, expected); });
}
std::string Session::load_json(const std::string &document) {
  return guarded([&]() { return load(Json::parse(document)); });
}
std::string Session::save_with_presentation_json(const std::string &presentation) const {
  auto document = save();
  document["presentation"] = Json::parse(presentation);
  return document.dump();
}
} // namespace twisty
