// Numeric runtime consumer: no JSON, source compiler, interpreter, or framework.
#include <twisty/completion.hpp>
#include <iostream>

using namespace twisty;
namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
template <class F> void reject(F query, const char *code) {
  try { query(); }
  catch (const core::Error &error) {
    check(error.code == code, "Unexpected runtime diagnostic");
    return;
  }
  throw std::runtime_error("Invalid runtime query was accepted");
}
} // namespace
int main() {
  try {
    core::ProgramData data;
    data.piece_count = 2;
    data.placement_count = 3;
    data.initial = {0, 2};
    data.finite_sets = {{1}, {2}, {0}};
    data.predicates = {{core::Opcode::in, core::Bank::choice, 0, 0},
                       {core::Opcode::in, core::Bank::choice, 0, 1},
                       {core::Opcode::in, core::Bank::state, 0, 2}};
    data.maps = {{{0, 1}, {1, 0}, {2, 2}}};
    data.actions = {{0, 0, 2, 0}, {1, 0, 3, 0}};
    data.requests = {{{3}, 0, 2, 4}};
    runtime::Bindings bindings;
    bindings.puzzle_id = "numeric-example";
    bindings.choice_slots = {"slot"};
    bindings.choice_values = {{"idle", "first", "second"}};
    bindings.requests = {{"turn", {{"default", {1}, 4}}, 0}};
    bindings.movements = {{{0, 0}, {0, 1}, {1, 1}}};
    runtime::CompiledPuzzle puzzle{core::Program(std::move(data)), std::move(bindings)};
    const auto initial = puzzle.program.data().initial;
    const auto choices = runtime::bind_profile(puzzle, 0, initial);
    check(choices == core::Choices{1}, "Default completion differs");
    const auto move = puzzle.program.plan(0, initial, *choices);
    check(move.transition && move.transition->after == core::State{1, 2}, "Numeric execution differs");
    check(!runtime::bind_profile(puzzle, 0, move.transition->after), "Unavailable profile was selected");
    check(runtime::rejection_inputs(puzzle.program, 0, move.transition->after, *choices) ==
              std::vector<core::Index>{0}, "Rejection proof differs");
    check(runtime::discover(puzzle.program, 0, initial, 3, 3).choices.size() == 3, "Discovery differs");
    check(runtime::movement_participates(puzzle, 0, 1, 0, 0), "Moving participant missing");
    check(!runtime::movement_participates(puzzle, 0, 1, 1, 2), "Transport support ignored explicit piece scope");
    check(runtime::movement_participates(puzzle, 0, 2, 1, 2), "Self-loop participant missing");
    check(!runtime::movement_participates(puzzle, 0, 0, 1, 2), "Idle choice acquired participation");
    check(initial == core::State{0, 2}, "Runtime query mutated caller state");
    reject([&] { runtime::movement_participates(puzzle, 0, 1, 2, 0); }, "core.input");
    reject([&] { runtime::movement_participates(puzzle, 1, 0, 0, 0); }, "runtime.binding");
    puzzle.bindings.movements[0][1].first_action = core::no_index;
    reject([&] { runtime::movement_participates(puzzle, 0, 1, 0, 0); }, "runtime.binding");
    std::cout << "Compiler-independent runtime completion, discovery and participation passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
