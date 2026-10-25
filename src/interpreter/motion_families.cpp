#include "geometry_program.hpp"
#include <cmath>
#include <numbers>

namespace twisty {
namespace {
void require(bool ok) {
  if (!ok)
    throw DiagnosticError("realization.motion_family", "/motionFamilies",
                          "Invalid rotation generator or path binding.");
}
GeometryPoint point(const Json &j) {
  require(j.is_array() && j.size() == 3);
  auto p = j.get<GeometryPoint>();
  for (auto x : p)
    require(std::isfinite(x) && std::abs(x) <= 100);
  return p;
}
} // namespace
void validate_motion_families(const Json &r) {
  if (!r.contains("motionFamilies"))
    return;
  const auto caps = r.at("requiredCapabilities").get<std::vector<std::string>>();
  require(std::find(caps.begin(), caps.end(), "family-rotation-generators") != caps.end());
  const auto &m = r.at("motionFamilies");
  require(m.at("version") == 1 && m.at("families").is_object() && m.at("families").size() <= 256);
  for (const auto &[id, f] : m.at("families").items()) {
    require(!id.empty());
    auto a = point(f.at("axis"));
    auto p = point(f.at("pivot"));
    double n = 0;
    for (auto x : a)
      n += x * x;
    require(n > 1e-20);
    const auto space = f.at("space").get<std::string>();
    require(space == "world" || space == "local");
    require(f.at("coordinates").is_object() && f.at("coordinates").size() >= 2 &&
            f.at("coordinates").size() <= 128);
    for (const auto &[stop, angle] : f.at("coordinates").items()) {
      require(!stop.empty() && angle.is_number());
      const auto x = angle.get<double>();
      require(std::isfinite(x) && std::abs(x) <= 100);
    }
    if (r.at("kind").get<std::string>().ends_with("spherical"))
      require(p == GeometryPoint{0, 0, 0});
  }
}
GeometryRotation family_rotation(const Json &r, const Json &b) {
  const auto &families = r.at("motionFamilies").at("families");
  const auto id = b.at("family").get<std::string>();
  require(families.contains(id));
  const auto &f = families.at(id);
  const auto &coordinates = f.at("coordinates");
  const auto from = b.at("from").get<std::string>(), to = b.at("to").get<std::string>();
  require(coordinates.contains(from) && coordinates.contains(to));
  require(b.at("winding").is_number_integer() && b.at("winding") >= -16 && b.at("winding") <= 16);
  const auto winding = b.at("winding").get<int>();
  const auto rate = b.value("rate", 1.0);
  require(std::isfinite(rate) && std::abs(rate) <= 100);
  auto axis = point(f.at("axis"));
  double n = 0;
  for (auto x : axis)
    n += x * x;
  require(n > 1e-20);
  for (auto &x : axis)
    x /= std::sqrt(n);
  const auto angle = rate * (coordinates.at(to).get<double>() - coordinates.at(from).get<double>() +
                             2 * std::numbers::pi * winding);
  require(std::isfinite(angle) && std::abs(angle) <= 100);
  return {axis, point(f.at("pivot")), angle, f.at("space") == "local"};
}
} // namespace twisty
