#pragma once
#include "twisty/authoring/compiler.hpp"
namespace twisty::language {
void read_compact(const Json &, core::ProgramData &);
Json write_compact(const core::ProgramData &);
void compact_cube_source(Json &, const Json &catalog);
} // namespace twisty::language
