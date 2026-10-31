#pragma once
#include "twisty/catalog.hpp"

namespace twisty::language {
// Authoring only: emit numeric execution, contract-2 correspondence, a
// versioned realization envelope, and the exact content-addressed payload text.
Json realization_artifacts(const std::string &source, const std::string &realization);
} // namespace twisty::language
