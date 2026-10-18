#pragma once
#include "twisty/authoring/cube.hpp"

namespace twisty::language {
// Finite authoring only; emits the same generic language as other libraries.
CubeModule gear_module(const std::vector<std::string> &geared_edges, const std::string &puzzle_id);
CubeModule gear_module_source(const Json &);
std::string gear_module_json(const std::string &source);
} // namespace twisty::language
