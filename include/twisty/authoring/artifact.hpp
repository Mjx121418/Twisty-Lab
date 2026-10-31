#pragma once
#include "twisty/catalog.hpp"

namespace twisty::language {
// Reference authoring adapter. Produces three source-free documents; the
// consumer validates them independently and never calls this factory.
Json cube_artifacts(bool bandaged);
std::string cube_artifacts_json(bool bandaged);
} // namespace twisty::language
