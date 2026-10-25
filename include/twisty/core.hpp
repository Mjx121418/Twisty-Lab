#pragma once

// This executor has no dependency on the authoring language, JSON, or geometry.
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace twisty::core {
using Index = std::uint32_t;
using State = std::vector<Index>;
using Choices = std::vector<Index>;
using Ref = Index;
inline constexpr Ref false_ref = 0, true_ref = 1;

class Error : public std::runtime_error {
public:
  std::string code;
  Error(std::string code, std::string message);
};

enum class Bank : std::uint8_t { state, choice };
enum class Opcode : std::uint8_t { in, range };
struct Predicate {
  Opcode opcode = Opcode::in;
  Bank bank = Bank::state;
  Index input = 0, set = 0;
  Index lower = 0, upper = 0, first_child = 0, child_count = 0;
};
struct Row {
  Index from, to;
  bool operator==(const Row &) const = default;
};
inline constexpr Index no_index = std::numeric_limits<Index>::max();
struct Group {
  Index order = 0; // identity is element zero; products[g * order + h] = g h
  std::vector<Index> products, inverses;
};
enum class ActionKind : std::uint8_t { regular, tabulated, trivial };
struct FiniteAction {
  Index group = 0, size = 0;
  ActionKind kind = ActionKind::trivial;
  std::vector<Index> table;
};
struct PlacementCollection {
  Index action = 0;
  std::vector<Index> placements;
};
struct ReferenceSpace {
  Index size = 0;
};
// A partial injection between finite coordinate sets; the source may be larger.
struct PositionMap {
  Index source_space = 0, target_space = 0;
  std::vector<Row> rows;
  // Derived: out * base * in^-1. Dependencies precede their users.
  Index base = no_index, input_action = no_index, output_action = no_index;
  Index input_element = 0, output_element = 0;
};
struct ReferenceBinding {
  Index space = 0;
  std::vector<Index> placements;
  Index base = no_index, group = 0, element = 0;
};
struct TwistFamily {
  Index anchor_space = 0, reference_space = 0;
  std::vector<Index> positions;
};
enum class TransportKind : std::uint8_t { explicit_map, group, difference, join };
struct Transport {
  TransportKind kind = TransportKind::explicit_map;
  Index collection = 0, element = 0, support = no_index;
  Index family = 0, binding = 0, source = 0, target = 0;
  std::vector<Index> parts;
};
struct Action {
  Index piece, map;
  Ref selection;
  Index event;
};
struct Request {
  std::vector<Index> choice_sizes;
  Index first_action = 0, action_count = 0;
  Ref relation = true_ref;
};
struct ProgramData {
  Index piece_count = 0, placement_count = 0;
  State initial;
  std::vector<std::vector<Index>> finite_sets;
  std::vector<Predicate> predicates;
  std::vector<Ref> child_references;
  std::vector<std::vector<Row>> maps;
  std::vector<Action> actions;
  std::vector<Request> requests;
  Ref invariant = true_ref, goal = true_ref;
  // Empty extensions preserve the version-1 sparse program representation.
  std::vector<Group> groups;
  std::vector<FiniteAction> finite_actions;
  std::vector<PlacementCollection> collections;
  std::vector<ReferenceSpace> reference_spaces;
  std::vector<PositionMap> position_maps;
  std::vector<ReferenceBinding> reference_bindings;
  std::vector<TwistFamily> twist_families;
  std::vector<Transport> transports; // if present, exactly one descriptor per maps entry
};
struct PieceEvent {
  Index piece, from, to, event;
};
struct Transition {
  Index request;
  Choices choices;
  State before, after;
  std::vector<PieceEvent> actions;
};
enum class Rejection { none, relation, conflicting_writes, target_invariant };
struct Plan {
  std::optional<Transition> transition;
  Rejection rejection = Rejection::none;
};

// Validation happens at construction. Data cannot be mutated afterwards.
// Calls own their evaluation scratch and can share a const Program across threads.
class Program {
public:
  explicit Program(ProgramData data);
  const ProgramData &data() const {
    return data_;
  }
  bool valid(std::span<const Index> state) const;
  bool solved(std::span<const Index> state) const;
  Index act(Index collection, Index element, Index placement) const;
  std::optional<Index> transport(Index map, Index placement) const;
  // Forward A -> R, or inverse R -> A. Undefined rows are not participants.
  std::optional<Index> position_map(Index map, Index point, bool inverse = false) const;
  std::size_t transport_storage_bytes() const;
  Plan plan(Index request, std::span<const Index> state, std::span<const Index> choices) const;
  bool state_predicate(Ref root, std::span<const Index> state) const;
  // Optional diagnostics use this same evaluator, without adding guard kinds.
  // Values are 0/1 for nodes reachable from root; -1 marks unevaluated nodes.
  // This inspects a predicate, not request permission or assignment validity.
  std::vector<std::int8_t> predicate_trace(Ref root, std::span<const Index> state,
                                           std::span<const Index> choices = {}) const;

private:
  ProgramData data_;
  struct ChoiceActions {
    Index input, value;
    std::vector<Index> actions;
  };
  // Derived acceleration only; no serialized state or additional predicates.
  std::vector<ChoiceActions> choice_actions_;
  std::vector<Index> general_actions_;
  std::vector<Index> collection_of_, local_of_;
  std::vector<std::vector<Row>> position_inverses_, binding_inverses_;
  Index finite_action(Index action, Index element, Index point) const;
  std::optional<Index> position_map_unchecked(Index id, Index point, bool inverse) const;
  Index bind(Index id, Index reference, bool inverse) const;
  std::optional<Index> transport_unchecked(Index map, Index placement) const;
  void validate_transports();
  void check_state_shape(std::span<const Index> state) const;
};
} // namespace twisty::core
