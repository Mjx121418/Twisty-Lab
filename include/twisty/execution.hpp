#pragma once
#include "twisty/compiler.hpp"
#include "twisty/relational_completion.hpp"

namespace twisty {
// Migration boundary: the grouped State/Definition below are presentation and
// legacy wire adapters. Relational permission, validity, and goals use only the
// flat Program. No legacy planner is consulted by a relational execution.
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
  std::shared_ptr<const Definition> definition() const { return definition_; }
  bool relational() const { return static_cast<bool>(puzzle_); }
  Json definition_json() const;
  State decode(const Json &) const;
  void validate(const State &) const;
  bool is_solved(const State &) const;
  ExecutionPlan plan(const State &, const std::string &, const Json &choices = nullptr) const;
  ExecutionPlan inverse(const CompletedTransition &) const;
  Json encode(const CompletedTransition &) const;
  Json legal(const State &) const;

private:
  explicit Execution(std::shared_ptr<const Definition> definition) : definition_(std::move(definition)) {}
  Execution(const Json &source, const Json &catalog);
  std::shared_ptr<const Definition> definition_;
  std::optional<relational::CompiledPuzzle> puzzle_;
  std::vector<std::vector<Index>> global_of_local_;
  std::vector<Index> local_of_global_;
  std::vector<Index> flat_piece_of_display_, display_piece_of_flat_;
  std::map<std::string, Index> request_ids_;
  std::vector<std::map<std::string, std::string>> inverse_moves_;
  flat::State flatten(const State &) const;
  State display(const flat::State &) const;
};
// Compiles either an explicit relational package or a legacy source; failures
// retain the structured compiler diagnostic rather than crossing the ABI.
Json compile_execution_json(const std::string &);
} // namespace twisty
