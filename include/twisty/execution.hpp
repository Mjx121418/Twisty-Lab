#pragma once
#include "twisty/catalog.hpp"
#include "twisty/relational_completion.hpp"

namespace twisty {
// Grouped State/Definition are symbolic presentation and interpreter catalogs.
// Permission, validity, and goals are evaluated only by the domain-free Program.
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
  std::shared_ptr<const Definition> definition() const {
    return definition_;
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
  Execution(Json source, Json catalog);
  std::shared_ptr<const Definition> definition_;
  relational::CompiledPuzzle puzzle_;
  std::vector<std::vector<Index>> global_of_local_;
  std::vector<Index> local_of_global_;
  std::vector<Index> flat_piece_of_display_, display_piece_of_flat_;
  std::map<std::string, Index> request_ids_;
  bool articulated_events_ = false;
  std::map<std::pair<Index, std::string>, Index> event_operations_;
  std::vector<std::map<std::string, std::string>> inverse_moves_;
  flat::State flatten(const State &) const;
  State display(const flat::State &) const;
};
// Compiles an explicit relational package or Gear module; failures
// retain the structured compiler diagnostic rather than crossing the ABI.
Json compile_execution_json(const std::string &);
} // namespace twisty
