#include "twisty/geometry.hpp"
#include <algorithm>
#include <cmath>

namespace twisty {
namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw DiagnosticError("realization.flat_surface", "/flatSurface", message);
}
} // namespace

// Canonicalization is for validating geometric endpoints. Sampling retains a
// continuous lift; renderers choose images and apply the deck transformations.
Geometry::Frame Geometry::canonical_flat_frame(Frame m) const {
  const auto cell = [&](double v) {
    require(std::isfinite(v) && std::abs(v) < flat_side_ * 1000,
            "Flat-surface endpoint is outside the bounded chart range.");
    const auto nearest = std::round(v / flat_side_);
    return static_cast<long long>(std::abs(v - nearest * flat_side_) < 1e-8
                                      ? nearest : std::floor(v / flat_side_));
  };
  const auto row = cell(m[13]);
  m[13] -= row * flat_side_;
  if (flat_twisted_ && row % 2 != 0) {
    // Lift an O(2) reflection to a proper O(3) frame by reversing z too.
    // Assets are planar and renderers draw both sides.
    for (int c = 0; c < 4; ++c) {
      m[c * 4] = -m[c * 4];
      m[c * 4 + 2] = -m[c * 4 + 2];
    }
    m[12] += flat_side_;
  }
  m[12] -= cell(m[12]) * flat_side_;
  return m;
}

void Geometry::build_flat_surface(const Json &realization) {
  const auto &surface = realization.at("flatSurface");
  const auto topology = surface.at("topology").get<std::string>();
  require(topology == "torus" || topology == "klein-bottle", "Unknown square quotient topology.");
  flat_twisted_ = topology == "klein-bottle";
  flat_side_ = surface.at("sideLength").get<double>();
  require(std::isfinite(flat_side_) && flat_side_ > 0 && flat_side_ <= 100,
          "Square side length must be finite and in (0,100].");
  const auto &capabilities = realization.at("requiredCapabilities");
  require(capabilities.is_array(), "Rendering capabilities must be an array.");
  for (const auto *name : {"triangle-meshes", "rigid-transforms", "quotient-surface"})
    require(std::find(capabilities.begin(), capabilities.end(), name) != capabilities.end(),
            "Flat surfaces require mesh, frame and quotient-surface capabilities.");
  build_articulated(realization);
  const Frame horizontal{1,0,0,0, 0,1,0,0, 0,0,1,0, flat_side_,0,0,1};
  const Frame vertical = flat_twisted_
      ? Frame{-1,0,0,0, 0,1,0,0, 0,0,-1,0, flat_side_,flat_side_,0,1}
      : Frame{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,flat_side_,0,1};
  scene_["ambientSpace"] = "E2";
  scene_["flatSurface"] = {{"topology", topology},
                            {"fundamentalBounds", {0, 0, flat_side_, flat_side_}},
                            {"deckGenerators", {horizontal, vertical}},
                            {"twoSided", true}};
  if (surface.contains("cutRadius")) {
    const auto radius = surface.at("cutRadius").get<double>();
    require(std::isfinite(radius) && radius > 0 && radius < flat_side_ / 2,
            "Cut disks must be embedded in the quotient.");
    scene_["flatSurface"]["cutRadius"] = radius;
  }
  if (surface.contains("diskCenters")) {
    const auto &centers = surface.at("diskCenters");
    require(centers.is_object() && centers.size() <= 256, "Disk centers must be a bounded named map.");
    for (const auto &[name, center] : centers.items()) {
      require(!name.empty() && center.is_array() && center.size() == 2,
              "A named disk center requires a planar coordinate pair.");
      for (const auto &coordinate : center) {
        require(coordinate.is_number(), "Disk coordinates must be numeric.");
        const auto value = coordinate.get<double>();
        require(std::isfinite(value) && value >= 0 && value < flat_side_,
                "Disk centers must lie inside the fundamental square.");
      }
    }
    scene_["flatSurface"]["diskCenters"] = centers;
  }
}
} // namespace twisty
