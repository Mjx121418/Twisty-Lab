#pragma once
#include "../core.hpp"
#include <nlohmann/json.hpp>

namespace twisty::language {
using Json = nlohmann::json;
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
struct Bindings {
  std::string puzzle_id;
  std::vector<std::string> pieces, placements, events, choice_slots;
  std::vector<std::vector<std::string>> choice_values;
  std::vector<RequestBinding> requests;
};
struct CompiledPuzzle {
  core::Program program;
  Bindings bindings;
};

// Version 1 of the initial, expanded JSON AST. Text syntax and module expansion
// will use this compiler in later migration milestones.
CompiledPuzzle compile(const Json &source);
CompiledPuzzle compile_text(const std::string &source);
Json inspect(const CompiledPuzzle &puzzle);

// Portable experimental facade. Queries are pure; no state/history is owned here.
class Puzzle {
public:
  explicit Puzzle(const std::string &source);
  std::string inspect_json() const;
  std::string query_json(const std::string &query) const;

private:
  CompiledPuzzle puzzle_;
};
std::string compile_json(const std::string &source);
} // namespace twisty::language
