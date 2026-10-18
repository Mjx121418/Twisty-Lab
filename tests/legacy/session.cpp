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
  } catch (const core::Error &error) {
    return Json{{"status", "Invalid"},
                {"diagnostics",
                 Json::array({{{"reasonCode", error.code}, {"source", "/"}, {"message", error.what()}}})}}
        .dump();
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
std::string request_operation(const std::string &request) {
  const auto parsed = Json::parse(request);
  if (parsed.contains("choices") && !parsed.at("choices").is_array())
    throw DiagnosticError("request.choices", "/choices", "A supplied completion must be an array.");
  if (!parsed.at("parameters").is_object() || !parsed.at("parameters").empty())
    throw DiagnosticError("request.parameters", "/parameters",
                          "These primitives accept an empty parameter record.");
  return parsed.at("operation").get<std::string>();
}
} // namespace
Session::Session(const std::string &source) : Session(Execution::from_source(source)) {}
Session::Session(std::shared_ptr<const Execution> execution) : execution_(std::move(execution)) {
  if (!execution_)
    throw DiagnosticError("execution.context", "/definition", "A compiled execution context is required.");
  definition_ = execution_->definition();
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
                          {"solved", execution_->is_solved(state_)},
                          {"legalRequests", execution_->legal(state_)},
                          {"history", history},
                          {"cursor", cursor_},
                          {"canUndo", cursor_ > 0},
                          {"canRedo", cursor_ < history_.size()}};
  if (execution_->relational())
    (*cached_snapshot_)["candidateRequests"] = execution_->candidates(state_);
  cached_revision_ = revision_;
  return *cached_snapshot_;
}
Json Session::commit(const CompletedTransition &transition, bool truncate) {
  if (truncate && cursor_ < history_.size())
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
  state_ = transition.after;
  history_.push_back(transition);
  ++cursor_;
  ++revision_;
  return execution_->encode(transition);
}
Json Session::execute(const std::string &operation, const std::string &expected) {
  return execute_completed(operation, nullptr, expected);
}
Json Session::execute(const std::string &operation, const std::vector<std::string> &choices,
                      const std::string &expected) {
  return execute_completed(operation, choices, expected);
}
Json Session::execute_completed(const std::string &operation, const Json &choices,
                                const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  const auto result = execution_->plan(state_, operation, choices);
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
  std::vector<CompletedTransition> staged;
  auto temporary = state_;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    auto result = execution_->plan(policy == "transactional" ? temporary : state_, moves[i]);
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
  auto result = execution_->inverse(previous);
  if (!result.transition || result.transition->after != previous.before)
    throw DiagnosticError("history.inverse", "/history", "Inverse did not restore the full recorded state.");
  state_ = result.transition->after;
  --cursor_;
  ++revision_;
  return {{"status", "Committed"},
          {"transitions", Json::array({execution_->encode(*result.transition)})},
          {"snapshot", snapshot()}};
}
Json Session::redo(const std::string &expected) {
  if (stale(expected))
    return stale_result(revision());
  if (cursor_ == history_.size())
    return {{"status", "NoHistory"}};
  auto result = execution_->plan(state_, history_[cursor_].operation,
                                 execution_->relational() ? Json(history_[cursor_].choices) : Json{});
  if (!result.transition || result.transition->after != history_[cursor_].after)
    throw DiagnosticError("history.replay", "/history", "Redo differs from the recorded transition.");
  state_ = result.transition->after;
  ++cursor_;
  ++revision_;
  return {{"status", "Committed"},
          {"transitions", Json::array({execution_->encode(*result.transition)})},
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
  std::vector<CompletedTransition> staged;
  std::string excluded;
  for (std::uint32_t i = 0; i < length; ++i) {
    std::vector<std::string> candidates;
    for (const auto &request : execution_->legal(temporary))
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
    auto transition = *execution_->plan(temporary, selected).transition;
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
    if (execution_->relational())
      requests.back()["choices"] = history_[i].choices;
    if ((i + 1) % 100 == 0)
      checkpoints.push_back({{"index", i + 1},
                             {"state", encode_state(*definition_, history_[i].after)},
                             {"stateDigest", state_digest(*definition_, history_[i].after)}});
  }
  return {{"schemaVersion", execution_->relational() ? 2 : 1},
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
  if (document.at("schemaVersion") != (execution_->relational() ? 2 : 1) || document.at("kind") != "session")
    throw DiagnosticError(
        "session.schema", "/",
        "Session schema does not match this execution format. Legacy histories require explicit conversion.");
  const auto &logical = document.at("logical");
  if (logical.at("definitionDigest") != definition_->digest)
    throw DiagnosticError("definition.digest_mismatch", "/logical/definitionDigest",
                          "Saved session uses different puzzle semantics.");
  const auto origin = execution_->decode(logical.at("originState"));
  auto temporary = origin;
  std::vector<CompletedTransition> history;
  if (logical.at("requests").size() > 100000)
    throw DiagnosticError("history.limit", "/logical/requests", "History exceeds 100000 requests.");
  for (const auto &request : logical.at("requests")) {
    if (!request.at("parameters").is_object() || !request.at("parameters").empty())
      throw DiagnosticError("request.parameters", "/logical/requests",
                            "These primitives do not accept parameters.");
    if (execution_->relational() && (!request.contains("choices") || !request.at("choices").is_array()))
      throw DiagnosticError("history.choices", "/logical/requests",
                            "Relational replay requires recorded choices.");
    auto result = execution_->plan(temporary, request.at("operation").get<std::string>(),
                                   request.value("choices", Json{}));
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
    if (execution_->decode(checkpoint.at("state")) != expected_state ||
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
  if (!execution_->relational())
    return execution_->definition_json().dump();
  // Emit the same sorted canonical object as definition_json().dump(), without
  // cloning a large AST just to add its digest to the serialized document.
  std::string result = "{";
  bool digest = false;
  const auto append = [&](const std::string &key, const Json &value) {
    if (result.size() > 1)
      result += ',';
    result += Json(key).dump() + ':' + value.dump();
  };
  for (const auto &[key, value] : definition_->canonical.items()) {
    if (!digest && key >= "definitionDigest") {
      append("definitionDigest", definition_->digest);
      digest = true;
    }
    if (key != "definitionDigest")
      append(key, value);
  }
  if (!digest)
    append("definitionDigest", definition_->digest);
  result += '}';
  return result;
}
std::string Session::execute_json(const std::string &request, const std::string &expected) {
  return guarded([&]() {
    return execute_completed(request_operation(request), Json::parse(request).value("choices", Json{}),
                             expected);
  });
}
std::string Session::plan_json(const std::string &request, const std::string &state) const {
  return guarded([&]() -> Json {
    const auto result = execution_->plan(execution_->decode(Json::parse(state)), request_operation(request),
                                         Json::parse(request).value("choices", Json{}));
    if (!result.transition)
      return result.blocked;
    const auto transition = execution_->encode(*result.transition);
    return {{"status", "Legal"}, {"transition", transition}, {"transitionRecord", transition.dump()}};
  });
}
std::string Session::validate_state_json(const std::string &state) const {
  return guarded([&]() -> Json {
    execution_->decode(Json::parse(state));
    return {{"status", "Valid"}};
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
std::string Session::convert_legacy_json(const std::string &text,
                                         const std::string &legacy_definition) const {
  return guarded([&]() -> Json {
    if (!execution_->relational())
      throw DiagnosticError("session.conversion", "/", "Conversion requires a relational destination.");
    const auto document = Json::parse(text);
    // Requiring a compiled schema-1 definition keeps the old meaning explicit.
    const auto old_definition = load_definition(Json::parse(legacy_definition));
    Session previous(Execution::legacy(old_definition));
    previous.load(document);
    if (old_definition->id != definition_->id)
      throw DiagnosticError("session.conversion", "/puzzleId", "Puzzle IDs differ.");
    Session converted(execution_);
    converted.origin_ = execution_->decode(encode_state(*old_definition, previous.origin_));
    converted.state_ = converted.origin_;
    for (const auto &recorded : previous.history_) {
      const auto planned = execution_->plan(converted.state_, recorded.operation);
      if (!planned.transition || encode_state(*definition_, planned.transition->after) !=
                                     encode_state(*old_definition, recorded.after))
        throw DiagnosticError("session.conversion", "/logical/requests",
                              "New completion does not reproduce the legacy replay.");
      auto old_actions = encode_transition(*old_definition, recorded).at("pieceActions");
      auto new_actions = execution_->encode(*planned.transition).at("pieceActions");
      for (Index i = 0; i < new_actions.size(); ++i) {
        auto &action = new_actions[i];
        const auto event_op = execution_->event_operation(planned.transition->actions[i].piece,
                                                          action.at("event").get<std::string>());
        if (definition_->operations[event_op].transport != action.at("transport").get<std::string>())
          throw DiagnosticError("session.conversion", "/logical/requests",
                                "New event differs from the legacy transport.");
        action.erase("event");
      }
      const auto sort = [](Json &actions) {
        std::sort(actions.begin(), actions.end(), [](const auto &a, const auto &b) {
          return a.at("pieceId").template get<std::string>() < b.at("pieceId").template get<std::string>();
        });
      };
      sort(old_actions);
      sort(new_actions);
      if (old_actions != new_actions)
        throw DiagnosticError("session.conversion", "/logical/requests",
                              "New completion has different participants or transports.");
      converted.commit(*planned.transition);
    }
    converted.cursor_ = previous.cursor_;
    converted.state_ =
        converted.cursor_ == 0 ? converted.origin_ : converted.history_[converted.cursor_ - 1].after;
    converted.revision_ = parse_revision(document.at("logical").at("revision").get<std::string>());
    auto result = converted.save();
    result["presentation"] = document.at("presentation");
    return {{"status", "Converted"}, {"session", result}};
  });
}
std::string Session::save_with_presentation_json(const std::string &presentation) const {
  auto document = save();
  document["presentation"] = Json::parse(presentation);
  return document.dump();
}
} // namespace twisty
