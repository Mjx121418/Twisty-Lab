#pragma once
#include "twisty/authoring/compiler.hpp"
#include "../compact.hpp"
namespace twisty::language {
using runtime::read_compact;
using runtime::write_compact;
void compact_cube_source(Json &, const Json &catalog);
} // namespace twisty::language
