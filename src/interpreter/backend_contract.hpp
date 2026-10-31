#pragma once
#include "twisty/geometry_backend.hpp"
#include <span>

namespace twisty::detail {
enum class GeometryEngine { spatial, spherical4 };
struct GeometryBackendDescriptor {
  const char *id;
  int version;
  GeometryEngine engine;
  bool envelope, cube, diagram, polyhedral, spherical, articulated, flat;
  std::span<const char *const> capabilities;
};
std::span<const GeometryBackendDescriptor> geometry_backend_descriptors();
const GeometryBackendDescriptor &geometry_backend(const std::string &id);
void validate_backend_capabilities(const GeometryBackendDescriptor &, const Json &capabilities);
Json geometry_backend_kinds(bool include_legacy = true);
} // namespace twisty::detail
