#pragma once
#include "twisty/catalog.hpp"

namespace twisty { class Execution; }
namespace twisty::language {
// Reference authoring adapter. Produces three source-free documents; the
// consumer validates them independently and never calls this factory.
Json cube_artifacts(bool bandaged);
std::string cube_artifacts_json(bool bandaged);
// Contract 2 correspondence lowering for a source-compiled execution context.
Json interpretation_artifact(const Execution &);
Json interpretation_artifact(const Execution &, const Json &abstract_artifact);
Json execution_artifacts(const std::string &source);
} // namespace twisty::language
