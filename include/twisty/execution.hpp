#pragma once
#include "twisty/catalog.hpp"
#include "twisty/completion.hpp"

namespace twisty {
namespace artifact { class Artifact; }
class ControlCatalog;
class Controller;
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
  static std::shared_ptr<const Execution> from_artifact(const std::string &);
  std::shared_ptr<const Execution> with_interpretation(const std::string &) const;
  bool source_free() const { return static_cast<bool>(artifact_); }
  std::shared_ptr<const runtime::CompiledPuzzle> compiled_puzzle() const { return puzzle_; }
  const std::string &interpretation_digest() const { return interpretation_digest_; }
  std::shared_ptr<const Definition> definition() const {
    return definition_;
  }
  Json definition_json() const;
  // Numeric artifact encoding; source loading remains in the authoring adapter.
  Json artifact_json() const;
  State decode(const Json &) const;
  void validate(const State &) const;
  bool is_solved(const State &) const;
  ExecutionPlan plan(const State &, const std::string &, const Json &choices = nullptr) const;
  ExecutionPlan inverse(const CompletedTransition &) const;
  Json encode(const CompletedTransition &) const;
  Json legal(const State &) const;
  // Available default completions, including candidates rejected by the relation.
  Json candidates(const State &) const;
  std::shared_ptr<const ControlCatalog> controls() const { return controls_; }
  bool articulated_events() const {
    return articulated_events_;
  }
  // Resolves an opaque participation event to a certified interpreter track.
  Index event_operation(Index piece, const std::string &event) const;
  Index event_operation(const CompletedTransition &, Index action) const;

private:
  friend class Controller;
  friend class GeometryDefinition;
  Execution(Json source, Json catalog);
  explicit Execution(std::shared_ptr<const artifact::Artifact>);
  void bind_interpretation(const Json &);
  std::shared_ptr<const artifact::Artifact> artifact_;
  std::string interpretation_digest_;
  Index interpretation_version_ = 0;
  struct Certificate {
    Index domain, choice, request, event;
    std::vector<Index> pieces;
  };
  std::vector<Certificate> certificates_;
  std::vector<Index> interpretation_slots_;
  std::vector<std::map<Index, Index>> choice_certificates_;
  std::shared_ptr<const Definition> definition_;
  std::shared_ptr<const runtime::CompiledPuzzle> puzzle_;
  std::shared_ptr<const ControlCatalog> controls_;
  std::vector<std::vector<Index>> global_of_local_;
  std::vector<Index> local_of_global_;
  std::vector<Index> core_piece_of_display_, display_piece_of_core_;
  std::map<std::string, Index> request_ids_;
  bool articulated_events_ = false;
  std::map<std::pair<Index, std::string>, Index> event_operations_;
  std::vector<std::map<std::string, std::string>> inverse_moves_;
  core::State flatten(const State &) const;
  State display(const core::State &) const;
  // Controller inputs have already passed the public state/session checks.
  std::vector<std::string> participating_moves(const State &, Index piece) const;
};
// Compiles an explicit relational package or Gear module; failures
// retain the structured compiler diagnostic rather than crossing the ABI.
Json compile_execution_json(const std::string &);
} // namespace twisty
