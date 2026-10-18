#pragma once
#include "twisty/authoring/compiler.hpp"

namespace twisty::language {
// Authoring library: returns an ordinary version-1 AST and separate exact
// catalog/port/event bindings. Neither executor contains cube-specific logic.
struct CubeModule {
  Json source;
  Json catalog;
};
CubeModule cube_module(bool bandaged);
std::string cube_module_json(bool bandaged);
} // namespace twisty::language
