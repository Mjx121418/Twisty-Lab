#pragma once
#include "twisty/catalog.hpp"

namespace twisty::detail {
// Rebind explicit foreign keys only. No identifier is parsed for motion data.
void remap_realization_requests(Json &, const std::map<std::string, std::string> &);
} // namespace twisty::detail
