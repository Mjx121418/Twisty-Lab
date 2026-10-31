#include "backend_registry.hpp"
#include "geometry_compiler.hpp"

namespace twisty::detail {
namespace {
void validate(const GeometryBackendDescriptor &descriptor, const Json &geometry) {
  validate_backend_capabilities(descriptor, geometry.at("requiredCapabilities"));
}
std::shared_ptr<const GeometryProgram> spatial(std::shared_ptr<const Execution> execution,
    const Json &geometry, const std::string &digest) {
  return GeometryCompiler(std::move(execution), geometry, digest).compile();
}
std::shared_ptr<const GeometryProgram> spherical4(std::shared_ptr<const Execution> execution,
    const Json &geometry, const std::string &digest) {
  const auto compiled = compile_s3(execution, geometry);
  if (!execution->source_free() || digest.empty()) return compiled;
  auto program = std::make_shared<GeometryProgram>(*compiled);
  program->scene["executionDigest"] = execution->definition()->digest;
  program->scene["interpretationDigest"] = execution->interpretation_digest();
  program->scene["realizationDigest"] = digest;
  program->scene["sceneId"] = geometry.at("id").get<std::string>() + ":" + digest;
  return program;
}
}
GeometryBackendRegistration geometry_backend_registration(const std::string &id) {
  const auto &descriptor = geometry_backend(id);
  return {descriptor, validate, descriptor.engine == GeometryEngine::spherical4 ? spherical4 : spatial};
}
std::shared_ptr<const GeometryProgram> compile_geometry_backend(std::shared_ptr<const Execution> execution,
    const Json &geometry, const std::string &digest) {
  const auto registration = geometry_backend_registration(geometry.at("kind").get<std::string>());
  registration.validate(registration.descriptor, geometry);
  return registration.compile(std::move(execution), geometry, digest);
}
} // namespace twisty::detail
