#pragma once
#include "twisty/core.hpp"

// Runtime value descriptors; shared as const after construction. No JSON,
// source compiler, or geometry dependency.
namespace twisty::runtime {
struct Profile {
  std::string id;
  core::Choices choices;
  core::Ref when = core::true_ref;
};
struct RequestBinding {
  std::string id;
  std::vector<Profile> profiles;
  std::optional<core::Index> default_profile;
};
struct MovementBinding {
  core::Index first_action = 0, action_count = 0;
};
struct Bindings {
  std::string puzzle_id;
  std::vector<std::string> pieces, placements, events, choice_slots;
  std::vector<std::vector<std::string>> choice_values;
  std::vector<RequestBinding> requests;
  std::vector<std::vector<core::Index>> move_maps;
  std::vector<std::string> collection_names;
  // Indexed by slot and local choice, including an empty idle range at zero.
  // These shared action slices preserve explicit per-move piece scopes.
  std::vector<std::vector<MovementBinding>> movements;
};
struct CompiledPuzzle {
  core::Program program;
  Bindings bindings;
  // Legacy inspection/definition encoding tag; never consulted for permission.
  int source_version = 1;
};

// Structural participation only: inspect a local move's compiled action scope
// and exact transport support, including self-loops. Does not select a profile,
// evaluate legality, or mutate state. Piece/placement use global core indices.
bool movement_participates(const CompiledPuzzle &, core::Index slot, core::Index choice,
                          core::Index piece, core::Index placement);
} // namespace twisty::runtime
