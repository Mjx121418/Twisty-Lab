#pragma once

// This executor has no dependency on the authoring language, JSON, or geometry.
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace twisty::flat {
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
  Plan plan(Index request, std::span<const Index> state, std::span<const Index> choices) const;
  bool state_predicate(Ref root, std::span<const Index> state) const;

private:
  ProgramData data_;
  void check_state_shape(std::span<const Index> state) const;
};
} // namespace twisty::flat
