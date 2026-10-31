#pragma once
#include "twisty/runtime.hpp"
#include <nlohmann/json.hpp>

namespace twisty::language {
using Json = nlohmann::json;
// Source-facing names remain available; their data is owned by the runtime.
using runtime::Profile;
using runtime::RequestBinding;
using runtime::Bindings;
using runtime::CompiledPuzzle;

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
