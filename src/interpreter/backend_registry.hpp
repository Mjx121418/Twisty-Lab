#pragma once
#include "backend_contract.hpp"
#include "geometry_program.hpp"

namespace twisty::detail {
struct GeometryBackendRegistration {
  const GeometryBackendDescriptor &descriptor;
  void (*validate)(const GeometryBackendDescriptor &, const Json &);
  std::shared_ptr<const GeometryProgram> (*compile)(std::shared_ptr<const Execution>, const Json &, const std::string &);
};
GeometryBackendRegistration geometry_backend_registration(const std::string &);
std::shared_ptr<const GeometryProgram> compile_geometry_backend(std::shared_ptr<const Execution>,
  const Json &, const std::string &digest = {});
} // namespace twisty::detail
