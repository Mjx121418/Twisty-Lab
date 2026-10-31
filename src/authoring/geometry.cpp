#include "twisty/geometry.hpp"

namespace twisty {
GeometryDefinition::GeometryDefinition(const std::string &definition, const std::string &realization)
    : GeometryDefinition(Execution::from_source(definition), realization) {}
GeometryInstance::GeometryInstance(const std::string &definition, const std::string &realization)
    : GeometryInstance(std::make_shared<GeometryDefinition>(definition, realization)) {}
} // namespace twisty
