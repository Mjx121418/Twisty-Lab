#include "twisty/runtime.hpp"

namespace twisty::runtime {
bool movement_participates(const CompiledPuzzle &puzzle, core::Index slot, core::Index choice,
                          core::Index piece, core::Index placement) {
  const auto &data = puzzle.program.data();
  if (piece >= data.piece_count || placement >= data.placement_count)
    throw core::Error("core.input", "Movement support requires known piece and placement indices");
  const auto &movements = puzzle.bindings.movements;
  if (slot >= movements.size() || choice >= movements[slot].size())
    throw core::Error("runtime.binding", "Unknown local movement binding");
  const auto &movement = movements[slot][choice];
  if (movement.first_action > data.actions.size() ||
      movement.action_count > data.actions.size() - movement.first_action)
    throw core::Error("runtime.binding", "Movement action slice is outside the program");
  for (core::Index i = 0; i < movement.action_count; ++i) {
    const auto &action = data.actions[movement.first_action + i];
    if (action.piece == piece && puzzle.program.transport(action.map, placement))
      return true;
  }
  return false;
}
} // namespace twisty::runtime
