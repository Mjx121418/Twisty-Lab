#pragma once
#include "twisty/catalog.hpp"

namespace twisty::language {
// Produces immutable execution/controller/presentation data and optional views.
// The second argument is a JSON array of legacy realization documents.
Json runtime_artifacts(const std::string &source, const std::string &realizations);
std::string runtime_artifacts_json(const std::string &source, const std::string &realizations);
// Validates a schema-2 save against its exact source definition, replays its
// recorded completions, and validates a schema-3 save for the same program.
std::string migrate_runtime_session_json(const std::string &source, const std::string &save);
} // namespace twisty::language
