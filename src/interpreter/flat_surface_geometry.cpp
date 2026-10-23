#include "geometry_compiler.hpp"
#include <algorithm>
#include <cmath>

namespace twisty {
namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw DiagnosticError("realization.flat_surface", "/flatSurface", message);
}
} // namespace

bool GeometryCompiler::flat_chart_contains(double x, double y) const {
  if (flat_pillowcase_)
    return std::isfinite(x) && std::isfinite(y) &&
           std::abs(x - flat_side_ / 2) + std::abs(y - flat_side_ / 2) <= flat_side_ / 2 + 1e-9;
  const auto u = flat_rhombus_ ? x - y / std::sqrt(3.) : x;
  const auto v = flat_rhombus_ ? 2 * y / std::sqrt(3.) : y;
  if (flat_333_)
    return std::isfinite(u) && std::isfinite(v) && u >= -1e-9 && v >= -1e-9 &&
           u <= flat_side_ + 1e-9 && v <= flat_side_ + 1e-9;
  return std::isfinite(u) && std::isfinite(v) && u >= -1e-9 && v >= -1e-9 &&
         u < flat_side_ - 1e-9 && v < flat_side_ - 1e-9;
}

// Canonicalization is for validating geometric endpoints. Sampling retains a
// continuous lift; renderers choose images and apply the deck transformations.
GeometryCompiler::Frame GeometryCompiler::canonical_flat_frame(Frame m) const {
  if (flat_333_) {
    require(std::isfinite(m[12]) && std::isfinite(m[13]) &&
                std::abs(m[12]) < flat_side_ * 1000 && std::abs(m[13]) < flat_side_ * 1000,
            "Flat-surface endpoint is outside the bounded chart range.");
    Frame best{};
    std::array<long long, 6> best_score{};
    bool found = false;
    for (int turn = 0; turn < 3; ++turn) {
      const auto angle = turn * 2 * std::acos(-1.) / 3;
      auto lift = m;
      for (int col = 0; col < 4; ++col) {
        lift[col*4] = std::cos(angle)*m[col*4] - std::sin(angle)*m[col*4+1];
        lift[col*4+1] = std::sin(angle)*m[col*4] + std::cos(angle)*m[col*4+1];
      }
      const auto u = (lift[12] - lift[13]/std::sqrt(3.)) / flat_side_;
      const auto v = 2*lift[13]/(std::sqrt(3.)*flat_side_);
      const auto ix = -static_cast<long long>(std::floor((u-v)/3));
      const auto iy = -static_cast<long long>(std::floor((u+2*v)/3));
      for (auto i = ix-1; i <= ix+1; ++i)
        for (auto j = iy-1; j <= iy+1; ++j) {
          auto candidate = lift;
          candidate[12] += (i+j)*1.5*flat_side_;
          candidate[13] += (j-i)*std::sqrt(3.)/2*flat_side_;
          if (!flat_chart_contains(candidate[12], candidate[13]))
            continue;
          std::array<long long, 6> score{};
          int k = 0;
          for (int entry : {12, 13, 0, 1, 4, 5})
            score[k++] = std::llround(candidate[entry]*1e9);
          if (!found || score < best_score) {
            found = true;
            best_score = score;
            best = candidate;
          }
        }
    }
    require(found, "Cannot canonicalize the 333 endpoint.");
    return best;
  }
  if (flat_pillowcase_) {
    require(std::isfinite(m[12]) && std::isfinite(m[13]) &&
                std::abs(m[12]) < flat_side_ * 1000 && std::abs(m[13]) < flat_side_ * 1000,
            "Flat-surface endpoint is outside the bounded chart range.");
    Frame best{};
    std::array<long long, 6> best_score{};
    bool found = false;
    for (int sign : {1, -1}) {
      auto lift = m;
      if (sign == -1) {
        for (int col = 0; col < 4; ++col) {
          lift[col * 4] = -lift[col * 4];
          lift[col * 4 + 1] = -lift[col * 4 + 1];
        }
        lift[12] += flat_side_ / 2;
        lift[13] += flat_side_ / 2;
      }
      const auto ix = -static_cast<long long>(std::floor(lift[12] / flat_side_));
      const auto iy = -static_cast<long long>(std::floor(lift[13] / flat_side_));
      for (auto i = ix - 1; i <= ix + 1; ++i)
        for (auto j = iy - 1; j <= iy + 1; ++j) {
          auto candidate = lift;
          candidate[12] += i * flat_side_;
          candidate[13] += j * flat_side_;
          if (!flat_chart_contains(candidate[12], candidate[13]))
            continue;
          std::array<long long, 6> score{};
          int k = 0;
          for (int entry : {12, 13, 0, 1, 4, 5})
            score[k++] = std::llround(candidate[entry] * 1e9);
          if (!found || score < best_score) {
            found = true;
            best_score = score;
            best = candidate;
          }
        }
    }
    require(found, "Cannot canonicalize the pillowcase endpoint.");
    return best;
  }
  const auto cell = [&](double v) {
    require(std::isfinite(v) && std::abs(v) < flat_side_ * 1000,
            "Flat-surface endpoint is outside the bounded chart range.");
    const auto nearest = std::round(v / flat_side_);
    return static_cast<long long>(std::abs(v - nearest * flat_side_) < 1e-8
                                      ? nearest : std::floor(v / flat_side_));
  };
  if (flat_rhombus_) {
    double u = m[12] - m[13] / std::sqrt(3.), v = 2 * m[13] / std::sqrt(3.);
    const auto i = cell(u), j = cell(v);
    u -= i * flat_side_;
    v -= j * flat_side_;
    if (flat_twisted_ && (i + j) % 2 != 0) {
      std::swap(u, v);
      for (int c = 0; c < 3; ++c) {
        const auto x = m[c * 4], y = m[c * 4 + 1];
        m[c * 4] = .5 * x + std::sqrt(3.) / 2 * y;
        m[c * 4 + 1] = std::sqrt(3.) / 2 * x - .5 * y;
        m[c * 4 + 2] = -m[c * 4 + 2];
      }
    }
    m[12] = u + v / 2;
    m[13] = v * std::sqrt(3.) / 2;
    return m;
  }
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

void GeometryCompiler::build_flat_surface(const Json &realization) {
  const auto &surface = realization.at("flatSurface");
  const auto topology = surface.at("topology").get<std::string>();
  require(topology == "torus" || topology == "klein-bottle" || topology == "pillowcase" || topology == "orbifold-333",
          "Unknown flat quotient topology.");
  flat_twisted_ = topology == "klein-bottle";
  flat_pillowcase_ = topology == "pillowcase";
  flat_333_ = topology == "orbifold-333";
  const auto chart = surface.value("chart", "square");
  require(chart == "square" || chart == "rhombus-60" || chart == "pillowcase-square" || chart == "orbifold-333-rhombus",
          "Unknown fundamental chart.");
  require((chart == "pillowcase-square") == flat_pillowcase_,
          "The pillowcase topology requires its square orbifold chart.");
  require((chart == "orbifold-333-rhombus") == flat_333_,
          "The 333 topology requires its rhombus orbifold chart.");
  flat_rhombus_ = chart == "rhombus-60" || flat_333_;
  flat_side_ = surface.at("sideLength").get<double>();
  require(std::isfinite(flat_side_) && flat_side_ > 0 && flat_side_ <= 100,
          "Chart side length must be finite and in (0,100].");
  const auto &capabilities = realization.at("requiredCapabilities");
  require(capabilities.is_array(), "Rendering capabilities must be an array.");
  for (const auto *name : {"triangle-meshes", "rigid-transforms", "quotient-surface"})
    require(std::find(capabilities.begin(), capabilities.end(), name) != capabilities.end(),
            "Flat surfaces require mesh, frame and quotient-surface capabilities.");
  require(!(flat_pillowcase_ || flat_333_) ||
              std::find(capabilities.begin(), capabilities.end(), "quotient-orbifold") != capabilities.end(),
          "Cone points require quotient-orbifold rendering support.");
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
  if (flat_pillowcase_) {
    const auto half = flat_side_ / 2;
    const Frame involution{-1,0,0,0, 0,-1,0,0, 0,0,1,0, half,half,0,1};
    const Frame tile_holonomy{-1,0,0,0, 0,-1,0,0, 0,0,1,0, flat_side_,flat_side_,0,1};
    auto &out = scene_["flatSurface"];
    out["chart"] = chart;
    out["fundamentalPolygon"] = {{half,0}, {flat_side_,half}, {half,flat_side_}, {0,half}};
    out["latticeBasis"] = {{half,half}, {-half,half}};
    out["chartOrigin"] = {half,0};
    out["tileHolonomy"] = tile_holonomy;
    out["deckGenerators"] = {horizontal, vertical, involution};
    out["conePoints"] = {{half/2,half/2}, {half*1.5,half/2},
                         {half/2,half*1.5}, {half*1.5,half*1.5}};
  }
  if (flat_rhombus_) {
    const auto height = flat_side_ * std::sqrt(3.) / 2;
    const Frame reflection{.5,std::sqrt(3.)/2,0,0, std::sqrt(3.)/2,-.5,0,0,
                           0,0,-1,0, 0,0,0,1};
    auto glide = reflection;
    glide[12] = flat_side_;
    const Frame translation{1,0,0,0, 0,1,0,0, 0,0,1,0, flat_side_/2,-height,0,1};
    const Frame second_translation{1,0,0,0, 0,1,0,0, 0,0,1,0, flat_side_/2,height,0,1};
    auto &out = scene_["flatSurface"];
    out["chart"] = chart;
    out["fundamentalBounds"] = {0, 0, flat_side_ * 1.5, height};
    out["fundamentalPolygon"] = {{0,0}, {flat_side_,0}, {flat_side_*1.5,height}, {flat_side_/2,height}};
    out["latticeBasis"] = {{flat_side_,0}, {flat_side_/2,height}};
    if (flat_333_) {
      const Frame first_translation{1,0,0,0, 0,1,0,0, 0,0,1,0, flat_side_*1.5,-height,0,1};
      const Frame third_turn{-.5,std::sqrt(3.)/2,0,0, -std::sqrt(3.)/2,-.5,0,0,
                             0,0,1,0, 0,0,0,1};
      auto second = first_translation;
      second[13] = height;
      out["deckGenerators"] = {first_translation, second, third_turn};
      out["conePoints"] = {{0,0}, {flat_side_,0}, {flat_side_/2,height}};
    } else if (flat_twisted_) {
      // T=a-b, G=S+a; G T G^-1=T^-1. S swaps rhombus basis
      // vectors. Adjacent edge pairs glue by G and S+b (no metric shear).
      out["deckGenerators"] = {translation, glide};
      out["tileReflection"] = reflection;
    } else {
      out["deckGenerators"] = {horizontal, second_translation};
    }
  }
  if (surface.contains("diskCenters")) {
    const auto &centers = surface.at("diskCenters");
    require(centers.is_object() && centers.size() <= 256, "Disk centers must be a bounded named map.");
    for (const auto &[name, center] : centers.items()) {
      require(!name.empty() && center.is_array() && center.size() == 2,
              "A named disk center requires a planar coordinate pair.");
      for (const auto &coordinate : center) {
        require(coordinate.is_number(), "Disk coordinates must be numeric.");
        require(std::isfinite(coordinate.get<double>()), "Disk coordinates must be finite.");
      }
      require(flat_chart_contains(center[0].get<double>(), center[1].get<double>()),
              "Disk centers must lie inside the fundamental chart.");
    }
    scene_["flatSurface"]["diskCenters"] = centers;
  }
  if (surface.contains("cutRadius")) {
    const auto radius = surface.at("cutRadius").get<double>();
    auto limit = flat_rhombus_ && flat_twisted_ ? flat_side_ * std::sqrt(3.) / 4 : flat_side_ / 2;
    if (flat_333_) {
      limit = flat_side_*std::sqrt(3.)/2;
      require(surface.contains("diskCenters") && !surface.at("diskCenters").empty(),
              "333 cut radii require named disk centers.");
      for (const auto &[name, center] : surface.at("diskCenters").items()) {
        const auto x = center[0].get<double>(), y = center[1].get<double>();
        const auto u = (x-y/std::sqrt(3.))/flat_side_, v = 2*y/(std::sqrt(3.)*flat_side_);
        const bool cone = std::abs(u-std::round(u))+std::abs(v-std::round(v)) < 1e-9;
        for (int i = -3; i <= 3; ++i)
          for (int j = -3; j <= 3; ++j) {
            if (!cone)
              limit = std::min(limit, std::hypot(x-(i+j*.5)*flat_side_, y-j*std::sqrt(3.)/2*flat_side_));
            for (int turn = 0; turn < 3; ++turn) {
              const auto angle = turn*2*std::acos(-1.)/3;
              const auto dx = std::cos(angle)*x-std::sin(angle)*y+(i+j)*1.5*flat_side_-x;
              const auto dy = std::sin(angle)*x+std::cos(angle)*y+(j-i)*std::sqrt(3.)/2*flat_side_-y;
              const auto distance = std::hypot(dx, dy);
              if (distance > 1e-9)
                limit = std::min(limit, distance/2);
            }
          }
      }
    }
    if (flat_pillowcase_ && surface.contains("diskCenters"))
      for (const auto &[name, center] : surface.at("diskCenters").items()) {
        require(center.is_array() && center.size() == 2 && center[0].is_number() && center[1].is_number(),
                "A named disk center requires a planar coordinate pair.");
        bool cone = true;
        double distance_squared = 0;
        for (const auto &coordinate : center) {
          const auto v = (coordinate.get<double>() - flat_side_ / 4) / (flat_side_ / 2);
          cone &= std::isfinite(v) && std::abs(v - std::round(v)) < 1e-9;
          const auto distance = (v - std::round(v)) * flat_side_ / 2;
          distance_squared += distance * distance;
        }
        if (!cone)
          limit = std::min(limit, std::sqrt(distance_squared));
      }
    require(std::isfinite(radius) && radius > 0 && radius < limit,
            "Cut disks must be embedded in the quotient.");
    scene_["flatSurface"]["cutRadius"] = radius;
  }
}
} // namespace twisty
