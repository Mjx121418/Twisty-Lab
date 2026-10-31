#pragma once
#include "twisty/catalog.hpp"

namespace twisty::runtime {
// Numeric transport codec shared by the artifact loader and source compiler.
void read_compact(const Json &, core::ProgramData &, int version);
Json write_compact(const core::ProgramData &, int version);
} // namespace twisty::runtime
