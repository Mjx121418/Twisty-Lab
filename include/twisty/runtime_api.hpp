#pragma once
#include "twisty/execution.hpp"

namespace twisty {
// Optional controller data is pinned independently of the numeric executable.
// Phase readers use global placement IDs; routes refer to compiled profiles.
std::shared_ptr<const Execution> runtime_execution(const std::string &artifact,
                                                  const std::string &controls);
std::string validate_runtime_json(const std::string &artifact, const std::string &controls);
} // namespace twisty
