#pragma once
#include "twisty/compiler.hpp"
#include "twisty/completion.hpp"

namespace twisty {
// Migration boundary: the grouped State/Definition below are presentation and
// legacy wire adapters. Relational permission, validity, and goals use only the
// core Program. No legacy planner is consulted by a relational execution.
struct CompletedTransition : Transition {
  std::vector<std::string> choices, events;
};
struct ExecutionPlan {
  std::optional<CompletedTransition> transition;
  Json blocked;
};
class Execution {
public:
  static std::shared_ptr<const Execution> from_source(const std::string &);
  static std::shared_ptr<const Execution> legacy(std::shared_ptr<const Definition>);
  std::shared_ptr<const Definition> definition() const {
    return definition_;
  }
  bool relational() const {
    return static_cast<bool>(puzzle_);
  }
  Json definition_json() const;
  State decode(const Json &) const;
  void validate(const State &) const;
  bool is_solved(const State &) const;
  ExecutionPlan plan(const State &, const std::string &, const Json &choices = nullptr) const;
  ExecutionPlan inverse(const CompletedTransition &) const;
  Json encode(const CompletedTransition &) const;
  Json legal(const State &) const;
  // Available default completions, including candidates rejected by the relation.
  Json candidates(const State &) const;
  bool articulated_events() const {
    return articulated_events_;
  }
  // Resolves an opaque participation event to a certified interpreter track.
  Index event_operation(Index piece, const std::string &event) const;

private:
  explicit Execution(std::shared_ptr<const Definition> definition) : definition_(std::move(definition)) {}
  Execution(Json source, Json catalog);
  std::shared_ptr<const Definition> definition_;
  std::optional<language::CompiledPuzzle> puzzle_;
  std::vector<std::vector<Index>> global_of_local_;
  std::vector<Index> local_of_global_;
  std::vector<Index> core_piece_of_display_, display_piece_of_core_;
  std::map<std::string, Index> request_ids_;
  bool articulated_events_ = false;
  std::map<std::pair<Index, std::string>, Index> event_operations_;
  std::vector<std::map<std::string, std::string>> inverse_moves_;
  core::State flatten(const State &) const;
  State display(const core::State &) const;
};
// Compiles either an explicit relational package or a legacy source; failures
// retain the structured compiler diagnostic rather than crossing the ABI.
Json compile_execution_json(const std::string &);
} // namespace twisty
