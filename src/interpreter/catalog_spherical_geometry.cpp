#include "twisty/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace twisty {
namespace {
using Point = std::array<double, 3>;
using UV = std::array<double, 2>;
struct Boundary {
  Point normal;
  double offset;
};
using Bounds = std::vector<Boundary>;
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point add(Point a, Point b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Point scaled(Point p, double s) {
  return {p[0] * s, p[1] * s, p[2] * s};
}
Point sub(Point a, Point b) { return add(a, scaled(b, -1)); }
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
void ensure(bool value, const std::string &message) {
  if (!value)
    throw DiagnosticError("realization.spherical", "/models", message);
}
Point unit(Point p) {
  const double length = std::sqrt(dot(p, p));
  ensure(std::isfinite(length) && length > 1e-12, "A spherical direction is degenerate.");
  return scaled(p, 1 / length);
}
bool inside(Point p, const Bounds &bounds, double tolerance = 1e-10) {
  return std::all_of(bounds.begin(), bounds.end(),
                     [&](const auto &b) { return dot(p, b.normal) >= b.offset - tolerance; });
}
// Intersect the declared surface hull with the unit sphere. Visual insets must never
// change the cut arrangement or the abstract participant classification.
Bounds hull(const std::vector<Point> &vertices) {
  Bounds bounds;
  for (std::size_t a = 0; a < vertices.size(); ++a)
    for (std::size_t b = a + 1; b < vertices.size(); ++b)
      for (std::size_t c = b + 1; c < vertices.size(); ++c) {
        const auto raw = cross(sub(vertices[b], vertices[a]), sub(vertices[c], vertices[a]));
        if (dot(raw, raw) < 1e-20)
          continue;
        auto n = unit(raw);
        double h = dot(n, vertices[a]), minimum = 0, maximum = 0;
        for (auto v : vertices) {
          minimum = std::min(minimum, dot(n, v) - h);
          maximum = std::max(maximum, dot(n, v) - h);
        }
        if (minimum < -1e-10 && maximum > 1e-10)
          continue;
        if (maximum <= 1e-10) {
          n = scaled(n, -1);
          h = -h;
        }
        // The outer cube faces are redundant (tangent) on the unit sphere.
        if (h <= -1 + 1e-10)
          continue;
        if (std::none_of(bounds.begin(), bounds.end(),
                         [&](const auto &other) { return dot(n, other.normal) > 1 - 1e-10; }))
          bounds.push_back({n, h});
      }
  ensure(bounds.size() >= 3, "The hull has no bounded spherical section.");
  return bounds;
}
std::vector<Point> corners(const Bounds &bounds) {
  std::vector<Point> points;
  for (std::size_t i = 0; i < bounds.size(); ++i)
    for (std::size_t j = i + 1; j < bounds.size(); ++j) {
      const auto &a = bounds[i], &b = bounds[j];
      const double aa = dot(a.normal, a.normal), bb = dot(b.normal, b.normal);
      const double ab = dot(a.normal, b.normal), det = aa * bb - ab * ab;
      if (det < 1e-12)
        continue;
      const auto p = add(scaled(a.normal, (a.offset * bb - b.offset * ab) / det),
                         scaled(b.normal, (b.offset * aa - a.offset * ab) / det));
      if (dot(p, p) > 1 + 1e-12)
        continue;
      const auto axis = cross(a.normal, b.normal);
      for (double sign : {-1.0, 1.0}) {
        const auto q = add(p, scaled(axis, sign * std::sqrt(std::max(0.0, 1 - dot(p, p)) / dot(axis, axis))));
        if (inside(q, bounds) &&
            std::none_of(points.begin(), points.end(), [&](Point other) {
              const auto delta = sub(q, other);
              return dot(delta, delta) < 1e-20;
            }))
          points.push_back(q);
      }
    }
  ensure(points.size() >= 3, "A spherical section has no nondegenerate region.");
  return points;
}
// Exact continuous extrema of a linear objective on a small-circle region:
// corners, extrema on each boundary circle, and interior stationary points.
std::array<double, 2> extrema(const Bounds &bounds, Point axis) {
  auto candidates = corners(bounds);
  for (const auto &b : bounds) {
    const double nn = dot(b.normal, b.normal);
    const auto p = scaled(b.normal, b.offset / nn);
    const auto tangent = sub(axis, scaled(b.normal, dot(axis, b.normal) / nn));
    if (dot(tangent, tangent) < 1e-20)
      continue;
    const auto v = scaled(unit(tangent), std::sqrt(std::max(0.0, 1 - dot(p, p))));
    for (auto q : {add(p, v), sub(p, v)})
      if (inside(q, bounds))
        candidates.push_back(q);
  }
  for (auto q : {unit(axis), scaled(unit(axis), -1)})
    if (inside(q, bounds))
      candidates.push_back(q);
  std::array<double, 2> result{1e100, -1e100};
  for (auto p : candidates) {
    result[0] = std::min(result[0], dot(axis, p));
    result[1] = std::max(result[1], dot(axis, p));
  }
  return result;
}
struct Patch {
  std::vector<Point> points;
  std::vector<std::uint32_t> indices;
};
Patch tessellate(const Bounds &bounds, int segments, int rings, double inset) {
  const auto vertices = corners(bounds);
  Point sum{};
  for (auto v : vertices)
    sum = add(sum, v);
  const auto z = unit(sum);
  const auto x = unit(cross(std::abs(z[2]) < 0.9 ? Point{0, 0, 1} : Point{0, 1, 0}, z));
  const auto y = cross(z, x);
  auto direction = [&](UV uv) { return unit(add(z, add(scaled(x, uv[0]), scaled(y, uv[1])))); };
  ensure(inside(z, bounds), "The spherical section has no star-shaped chart interior.");
  std::vector<double> angles;
  for (int i = 0; i < segments; ++i)
    angles.push_back(2 * std::numbers::pi * i / segments);
  for (auto v : vertices) {
    ensure(dot(v, z) > 1e-10, "The spherical section must fit inside one hemisphere.");
    double angle = std::atan2(dot(v, y), dot(v, x));
    if (angle < 0)
      angle += 2 * std::numbers::pi;
    angles.push_back(angle);
  }
  std::sort(angles.begin(), angles.end());
  angles.erase(std::unique(angles.begin(), angles.end(),
                           [](double a, double b) { return std::abs(a - b) < 1e-10; }), angles.end());
  if (angles.size() > 1 && angles.front() + 2 * std::numbers::pi - angles.back() < 1e-10)
    angles.pop_back();
  std::vector<UV> boundary;
  for (double angle : angles) {
    const UV ray{std::cos(angle), std::sin(angle)};
    double low = 0, high = 4;
    ensure(!inside(direction({ray[0] * high, ray[1] * high}), bounds), "The chart boundary is unbounded.");
    for (int iteration = 0; iteration < 60; ++iteration) {
      const double distance = (low + high) / 2;
      if (inside(direction({ray[0] * distance, ray[1] * distance}), bounds, 0))
        low = distance;
      else
        high = distance;
    }
    boundary.push_back({ray[0] * low * (1 - inset), ray[1] * low * (1 - inset)});
  }
  Patch patch;
  patch.points.push_back(z);
  const auto count = static_cast<std::uint32_t>(boundary.size());
  for (int ring = 1; ring <= rings; ++ring)
    for (auto uv : boundary)
      patch.points.push_back(direction({uv[0] * ring / rings, uv[1] * ring / rings}));
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto next = (i + 1) % count;
    patch.indices.insert(patch.indices.end(), {0, 1 + i, 1 + next});
    for (int ring = 1; ring < rings; ++ring) {
      const auto a = 1 + (ring - 1) * count + i, b = 1 + (ring - 1) * count + next;
      const auto c = 1 + ring * count + i, d = 1 + ring * count + next;
      patch.indices.insert(patch.indices.end(), {a, c, d, a, d, b});
    }
  }
  for (auto p : patch.points)
    ensure(inside(p, bounds, 1e-9), "Tessellation crosses a spherical cut.");
  return patch;
}
} // namespace

void Geometry::build_catalog_spherical(const Json &realization) {
  const double radius = realization.at("radius"), angle = realization.at("diskAngleDegrees");
  const double inset = realization.at("portInset"), lift = realization.at("surfaceLift");
  const int segments = realization.at("angularSegments"), rings = realization.at("radialSegments");
  ensure(std::isfinite(radius) && radius > 0 && radius <= 10 && std::abs(scale_ - radius) < 1e-10,
         "The display scale must equal the sphere radius in (0,10].");
  ensure(std::isfinite(angle) && angle > 0 && angle < 90 && std::isfinite(inset) && inset >= 0 &&
             inset < 0.25 && std::isfinite(lift) && lift > 0 && lift < 0.05 * radius &&
             segments >= 16 && segments <= 128 && segments % 4 == 0 && rings >= 2 && rings <= 12,
         "Invalid spherical disk dimensions or tessellation.");
  const double threshold = std::cos(angle * std::numbers::pi / 180);
  std::vector<Bounds> bounds;
  auto asset = [&](const std::string &id, const Bounds &region, bool port, int symmetry_order) {
    const auto patch = tessellate(region, segments, rings, port ? inset : 0);
    const auto position_offset = positions_.size(), index_offset = indices_.size();
    for (auto point : patch.points) {
      for (auto value : scaled(point, port ? 1 : 1 - lift / radius))
        positions_.push_back(static_cast<float>(value));
      for (auto value : point)
        normals_.push_back(static_cast<float>(value));
    }
    indices_.insert(indices_.end(), patch.indices.begin(), patch.indices.end());
    scene_["meshAssets"].push_back({{"id", id}, {"positionOffset", position_offset},
                                    {"vertexCount", patch.points.size()}, {"indexOffset", index_offset},
                                    {"indexCount", patch.indices.size()}, {"rotationSymmetryOrder", symmetry_order}});
  };
  for (const auto &model : models_) {
    bounds.push_back(hull(model.surface_vertices.empty() ? model.vertices : model.surface_vertices));
    for (const auto &b : bounds.back())
      ensure(std::abs(std::abs(b.offset) - threshold) < 1e-10,
             "Model cut circles disagree with the declared disk angle.");
    asset(model.id + "/body", bounds.back(), false, static_cast<int>(model.symmetries.size()));
    for (const auto &[port, n] : model.port_normals) {
      auto region = bounds.back();
      for (const auto &[other, other_n] : model.port_normals)
        if (other != port)
          region.push_back({sub(n, other_n), 0});
      asset(model.id + "/port/" + port, region, true, 1);
    }
  }
  scene_["ambientSpace"] = "S2";
  scene_["sphereRadius"] = radius;
  scene_["diskAngleDegrees"] = angle;
  scene_["diskCenters"] = Json::object();
  using Ranges = std::vector<std::vector<std::array<double, 2>>>;
  std::map<std::string, Ranges> ranges_by_grip;
  // Check continuous sections, not only mesh vertices. Blocked placements
  // intentionally retain volume/phase/catalog guards from the abstract core.
  for (Index op = 0; op < definition_->operations.size(); ++op) {
    const auto &operation = definition_->operations[op];
    const auto axis = tracks_[op].axis;
    if (scene_["diskCenters"].contains(operation.family)) {
      const auto other = scene_["diskCenters"][operation.family].get<Point>();
      ensure(dot(axis, other) > 1 - 1e-10, "A grip has inconsistent disk axes.");
    } else
      scene_["diskCenters"][operation.family] = axis;
    if (!ranges_by_grip.contains(operation.family)) {
      Ranges ranges(definition_->domains.size());
      for (Index domain = 0; domain < definition_->domains.size(); ++domain)
        for (const auto &f : placement_frames_[domain]) {
          const Point local_axis{f[0] * axis[0] + f[1] * axis[1] + f[2] * axis[2],
                                 f[4] * axis[0] + f[5] * axis[1] + f[6] * axis[2],
                                 f[8] * axis[0] + f[9] * axis[1] + f[10] * axis[2]};
          ranges[domain].push_back(extrema(bounds[model_of_domain_[domain]], local_axis));
        }
      ranges_by_grip[operation.family] = std::move(ranges);
    }
    for (Index domain = 0; domain < definition_->domains.size(); ++domain)
      for (Index q = 0; q < operation.roles[domain].size(); ++q) {
        const auto role = operation.roles[domain][q];
        if (role == 2)
          continue;
        const auto range = ranges_by_grip.at(operation.family)[domain][q];
        ensure(role == 1 ? range[0] >= threshold - 1e-9 : range[1] <= threshold + 1e-9,
               "A catalog participant or stationary section crosses the moving disk: " +
                   operation.id + "/" + definition_->domains[domain].placements[q].key +
                   " (role " + std::to_string(role) + ", range " + std::to_string(range[0]) +
                   ".." + std::to_string(range[1]) + ", threshold " + std::to_string(threshold) + ").");
      }
  }
  scene_["surfaceTransportVerified"] = true;
}
} // namespace twisty
