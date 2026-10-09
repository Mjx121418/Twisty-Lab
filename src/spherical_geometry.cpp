#include "twisty/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace twisty {
namespace {
using Point = std::array<double, 3>;
using Frame = std::array<double, 16>;
using UV = std::array<double, 2>;
struct Boundary {
  Point normal;
  double offset;
};
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Point scaled(Point p, double scale) {
  return {p[0] * scale, p[1] * scale, p[2] * scale};
}
Point add(Point a, Point b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Point direction(UV uv) {
  return scaled({uv[0], uv[1], 1}, 1 / std::sqrt(1 + uv[0] * uv[0] + uv[1] * uv[1]));
}
Point face_normal(const std::string &face) {
  if (face == "U")
    return {0, 1, 0};
  if (face == "R")
    return {1, 0, 0};
  if (face == "F")
    return {0, 0, 1};
  if (face == "D")
    return {0, -1, 0};
  if (face == "L")
    return {-1, 0, 0};
  if (face == "B")
    return {0, 0, -1};
  throw DiagnosticError("realization.spherical", face, "Expected a cube face attachment.");
}
void ensure(bool value, const std::string &source, const std::string &message) {
  if (!value)
    throw DiagnosticError("realization.spherical", source, message);
}
bool inside(Point point, const std::vector<Boundary> &bounds, double tolerance = 0) {
  return std::all_of(bounds.begin(), bounds.end(),
                     [&](const auto &bound) { return dot(point, bound.normal) >= bound.offset - tolerance; });
}
Point rotate(Point p, Point axis, double angle) {
  return add(add(scaled(p, std::cos(angle)), scaled(cross(axis, p), std::sin(angle))),
             scaled(axis, dot(axis, p) * (1 - std::cos(angle))));
}
Frame frame(Point x, Point y, Point z) {
  return {x[0], x[1], x[2], 0, y[0], y[1], y[2], 0, z[0], z[1], z[2], 0, 0, 0, 0, 1};
}
Point transform_point(const Frame &f, Point p) {
  return {f[0] * p[0] + f[4] * p[1] + f[8] * p[2], f[1] * p[0] + f[5] * p[1] + f[9] * p[2],
          f[2] * p[0] + f[6] * p[1] + f[10] * p[2]};
}

// One face-colored part of a region in the six-cap arrangement. The dominant
// local +Z face subdivides corner/edge regions into their labeled ports.
std::vector<Boundary> boundaries(int type, double threshold) {
  std::vector<Boundary> bounds{
      {{-1, 0, 1}, 0}, {{1, 0, 1}, 0}, {{0, -1, 1}, 0}, {{0, 1, 1}, 0}, {{0, 0, 1}, threshold}};
  if (type == 0) {
    bounds.push_back({{1, 0, 0}, -threshold});
    bounds.push_back({{-1, 0, 0}, -threshold});
  } else
    bounds.push_back({{1, 0, 0}, threshold});
  if (type < 2) {
    bounds.push_back({{0, 1, 0}, -threshold});
    bounds.push_back({{0, -1, 0}, -threshold});
  } else
    bounds.push_back({{0, type == 2 ? 1.0 : -1.0, 0}, threshold});
  return bounds;
}
std::vector<UV> vertices(const std::vector<Boundary> &bounds) {
  std::vector<UV> result;
  for (std::size_t i = 0; i < bounds.size(); ++i) {
    for (std::size_t j = i + 1; j < bounds.size(); ++j) {
      const auto &a = bounds[i], &b = bounds[j];
      const double aa = dot(a.normal, a.normal), bb = dot(b.normal, b.normal);
      const double ab = dot(a.normal, b.normal), determinant = aa * bb - ab * ab;
      if (determinant < 1e-12)
        continue;
      const auto p = add(scaled(a.normal, (a.offset * bb - b.offset * ab) / determinant),
                         scaled(b.normal, (b.offset * aa - a.offset * ab) / determinant));
      const double length = dot(p, p);
      if (length > 1 + 1e-12)
        continue;
      const auto axis = cross(a.normal, b.normal);
      for (const double sign : {-1.0, 1.0}) {
        const auto q = add(p, scaled(axis, sign * std::sqrt(std::max(0.0, 1 - length) / dot(axis, axis))));
        if (q[2] <= 0 || !inside(q, bounds, 1e-10))
          continue;
        const UV uv{q[0] / q[2], q[1] / q[2]};
        if (std::none_of(result.begin(), result.end(),
                         [&](UV other) { return std::hypot(uv[0] - other[0], uv[1] - other[1]) < 1e-10; }))
          result.push_back(uv);
      }
    }
  }
  ensure(result.size() >= 3, "/diskAngleDegrees", "A spherical port has no nondegenerate region.");
  return result;
}
struct Patch {
  std::vector<Point> points;
  std::vector<std::uint32_t> indices;
};
Patch tessellate(int type, double threshold, int segments, int rings, double inset) {
  const auto bounds = boundaries(type, threshold);
  const auto corners = vertices(bounds);
  UV center{};
  for (auto uv : corners) {
    center[0] += uv[0] / corners.size();
    center[1] += uv[1] / corners.size();
  }
  ensure(inside(direction(center), bounds, 1e-12), "/diskAngleDegrees", "Invalid patch interior.");
  std::vector<double> angles;
  for (int i = 0; i < segments; ++i)
    angles.push_back(2 * std::numbers::pi * i / segments);
  // Include every exact small-circle intersection, preventing a sampled edge
  // from skipping a corner where two boundary constraints meet.
  for (auto uv : corners) {
    double angle = std::atan2(uv[1] - center[1], uv[0] - center[0]);
    if (angle < 0)
      angle += 2 * std::numbers::pi;
    angles.push_back(angle);
  }
  std::sort(angles.begin(), angles.end());
  angles.erase(
      std::unique(angles.begin(), angles.end(), [](double a, double b) { return std::abs(a - b) < 1e-10; }),
      angles.end());
  std::vector<UV> boundary;
  for (const double angle : angles) {
    const UV ray{std::cos(angle), std::sin(angle)};
    double low = 0, high = 4;
    for (int iteration = 0; iteration < 60; ++iteration) {
      const double distance = (low + high) / 2;
      const auto p = direction({center[0] + ray[0] * distance, center[1] + ray[1] * distance});
      if (inside(p, bounds))
        low = distance;
      else
        high = distance;
    }
    boundary.push_back({center[0] + ray[0] * low * (1 - inset), center[1] + ray[1] * low * (1 - inset)});
  }
  Patch patch;
  patch.points.push_back(direction(center));
  const auto count = static_cast<std::uint32_t>(boundary.size());
  for (int ring = 1; ring <= rings; ++ring) {
    const double fraction = static_cast<double>(ring) / rings;
    for (auto uv : boundary)
      patch.points.push_back(direction(
          {center[0] + (uv[0] - center[0]) * fraction, center[1] + (uv[1] - center[1]) * fraction}));
  }
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
    ensure(inside(p, bounds, 1e-10), "/mesh", "Patch crosses a disk cut.");
  return patch;
}
} // namespace

void Geometry::build_spherical(const Json &realization) {
  const double radius = realization.at("radius").get<double>();
  const double angle = realization.at("diskAngleDegrees").get<double>();
  const double inset = realization.at("portInset").get<double>();
  const double lift = realization.at("surfaceLift").get<double>();
  const int segments = realization.at("angularSegments").get<int>();
  const int rings = realization.at("radialSegments").get<int>();
  const double threshold = std::cos(angle * std::numbers::pi / 180);
  ensure(std::isfinite(radius) && radius > 0 && radius <= 10, "/radius", "Expected a radius in (0,10].");
  ensure(std::isfinite(angle) && angle >= 55 && angle <= 85, "/diskAngleDegrees",
         "Use disk angular radii between 55 and 85 degrees to retain cube topology with a numerical margin.");
  ensure(std::isfinite(inset) && inset >= 0 && inset < 0.25, "/portInset",
         "Expected a port inset in [0,0.25).");
  ensure(std::isfinite(lift) && lift > 0 && lift < radius * 0.05, "/surfaceLift",
         "Expected a small positive surface lift.");
  ensure(segments >= 16 && segments <= 128 && segments % 4 == 0 && rings >= 2 && rings <= 12,
         "/angularSegments", "Use 16–128 angular segments in multiples of four and 2–12 radial segments.");
  ensure(!definition_->placement_relations, "/ruleModule", "Spherical cube disks require footprint rules.");
  for (const auto &operation : definition_->operations) {
    std::set<std::string> disk_cells;
    for (const auto &cell : definition_->cells)
      if (cell.find(operation.family) != std::string::npos)
        disk_cells.insert(cell);
    ensure(disk_cells.size() == 9 &&
               disk_cells == std::set<std::string>(operation.cells.begin(), operation.cells.end()),
           operation.id, "A spherical turn must select the entire face disk.");
  }
  scene_["ambientSpace"] = "S2";
  scene_["sphereRadius"] = radius;
  scene_["diskAngleDegrees"] = angle;
  scene_["fidelity"] = "spherical-surface-model";
  scene_["diskCenters"] = Json::object();
  for (const auto &face : {"U", "R", "F", "D", "L", "B"})
    scene_["diskCenters"][face] = face_normal(face);

  std::array<Patch, 4> patches;
  const std::array<std::string, 4> names{"center", "edge", "corner-positive", "corner-negative"};
  for (int type = 0; type < 4; ++type) {
    patches[type] = tessellate(type, threshold, segments, rings, 0);
    for (const bool port : {false, true}) {
      const auto patch = port ? tessellate(type, threshold, segments, rings, inset) : patches[type];
      const auto position_offset = positions_.size(), index_offset = indices_.size();
      for (auto point : patch.points) {
        const auto position = scaled(point, port ? radius : radius - lift);
        for (double value : position)
          positions_.push_back(static_cast<float>(value));
        for (double value : point)
          normals_.push_back(static_cast<float>(value));
      }
      indices_.insert(indices_.end(), patch.indices.begin(), patch.indices.end());
      scene_["meshAssets"].push_back({{"id", names[type] + (port ? "/port" : "/body")},
                                      {"positionOffset", position_offset},
                                      {"vertexCount", patch.points.size()},
                                      {"indexOffset", index_offset},
                                      {"indexCount", patch.indices.size()},
                                      {"rotationSymmetryOrder", type == 0 ? 4 : 1}});
    }
  }

  std::map<std::set<std::string>, std::size_t> home_ports;
  for (Index index = 0; index < definition_->pieces.size(); ++index) {
    const auto &piece = definition_->pieces[index];
    const auto &domain = definition_->domains[piece.domain];
    const auto &home = domain.placements[piece.home];
    for (const auto &[port, home_attachment] : home.ports) {
      std::vector<std::string> companions;
      for (const auto &[other, attachment] : home.ports)
        if (other != port && attachment.first == home_attachment.first)
          companions.push_back(other);
      std::set<std::string> component_faces{home_attachment.second};
      for (const auto &other : companions)
        component_faces.insert(home.ports.at(other).second);
      ++home_ports[component_faces];
      ensure(companions.size() <= 2, piece.id, "Expected cube center, edge, or corner components.");
      const auto z = face_normal(home_attachment.second);
      const auto x = companions.empty() ? (std::abs(z[0]) > 0.5 ? Point{0, 0, -1} : Point{1, 0, 0})
                                        : face_normal(home.ports.at(companions[0]).second);
      const auto y = cross(z, x);
      int type = companions.empty() ? 0
                 : companions.size() == 1
                     ? 1
                     : (dot(y, face_normal(home.ports.at(companions[1]).second)) > 0 ? 2 : 3);
      ensure(std::abs(dot(x, z)) < 1e-12 && std::abs(dot(y, y) - 1) < 1e-12, piece.id,
             "Port axes must be perpendicular.");
      const auto frame_index = static_cast<Index>(spherical_frames_.size());
      auto &frames = spherical_frames_.emplace_back();
      for (const auto &placement : domain.placements) {
        const auto target_z = face_normal(placement.ports.at(port).second);
        const auto target_x = companions.empty()
                                  ? (std::abs(target_z[0]) > 0.5 ? Point{0, 0, -1} : Point{1, 0, 0})
                                  : face_normal(placement.ports.at(companions[0]).second);
        const auto target_y = cross(target_z, target_x);
        const auto target_frame = frame(target_x, target_y, target_z);
        ensure(std::abs(dot(target_x, target_z)) < 1e-12 && std::abs(dot(target_y, target_y) - 1) < 1e-12,
               placement.key, "Invalid port frame.");
        if (companions.size() == 2)
          ensure(dot(target_y, face_normal(placement.ports.at(companions[1]).second)) * (type == 2 ? 1 : -1) >
                     1 - 1e-12,
                 placement.key, "Corner handedness changed.");
        std::set<std::string> attached_faces;
        attached_faces.insert(placement.ports.at(port).second);
        for (const auto &other : companions)
          attached_faces.insert(placement.ports.at(other).second);
        const auto &cell = placement.ports.at(port).first;
        std::set<std::string> cell_faces;
        for (char face : cell)
          cell_faces.insert(std::string(1, face));
        ensure(cell_faces == attached_faces, placement.key,
               "Spherical component differs from its cube cell.");
        frames.push_back(target_frame);
      }
      // Validate every participating placement, rather than only the home state.
      for (const auto &operation : definition_->operations) {
        const auto axis = face_normal(operation.family);
        const double turn =
            (operation.transport.ends_with(".counterclockwise") ? 1 : -1) * std::numbers::pi / 2;
        for (Index q = 0; q < domain.placements.size(); ++q) {
          const auto &placement = domain.placements[q];
          const bool participates =
              std::all_of(placement.footprint.begin(), placement.footprint.end(), [&](const auto &cell) {
                return std::find(operation.cells.begin(), operation.cells.end(), cell) !=
                       operation.cells.end();
              });
          if (!participates)
            continue;
          const auto target = operation.maps[piece.domain][q];
          for (auto point : patches[type].points)
            ensure(dot(transform_point(frames[q], point), axis) >= threshold - 1e-10, operation.id,
                   "A participant crosses the moving disk boundary.");
          for (const auto &[local_port, attachment] : placement.ports) {
            const auto actual = rotate(face_normal(attachment.second), axis, turn);
            const auto expected = face_normal(domain.placements[target].ports.at(local_port).second);
            const auto delta = add(actual, scaled(expected, -1));
            ensure(dot(delta, delta) < 1e-12, operation.id,
                   "Disk motion disagrees with abstract port transport.");
          }
          if (!companions.empty())
            for (int column = 0; column < 3; ++column) {
              const auto actual = rotate(
                  {frames[q][4 * column], frames[q][4 * column + 1], frames[q][4 * column + 2]}, axis, turn);
              const auto expected = Point{frames[target][4 * column], frames[target][4 * column + 1],
                                          frames[target][4 * column + 2]};
              const auto delta = add(actual, scaled(expected, -1));
              ensure(dot(delta, delta) < 1e-12, operation.id,
                     "Disk motion disagrees with the target patch frame.");
            }
        }
      }
      for (const bool colored : {false, true}) {
        const auto id = piece.id + (colored ? "/port/" : "/surface/") + port;
        parts_.push_back({index, port, colored ? "port" : "body", "", id, frame_index});
        scene_["visualParts"].push_back({{"visualPartId", id},
                                         {"pieceId", piece.id},
                                         {"portId", port},
                                         {"meshAssetId", names[type] + (colored ? "/port" : "/body")},
                                         {"materialBindingId", colored ? piece.labels.at(port) : "body"},
                                         {"role", colored ? "port" : "body"}});
      }
    }
  }
  ensure(home_ports.size() == 26, "/pieces", "The spherical cube requires all 26 surface regions.");
  for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
      for (int z = -1; z <= 1; ++z) {
        if (x == 0 && y == 0 && z == 0)
          continue;
        std::set<std::string> faces;
        if (x)
          faces.insert(x > 0 ? "R" : "L");
        if (y)
          faces.insert(y > 0 ? "U" : "D");
        if (z)
          faces.insert(z > 0 ? "F" : "B");
        ensure(home_ports.contains(faces) && home_ports.at(faces) == faces.size(), "/pieces",
               "Spherical regions must cover each center, edge, and corner exactly once.");
      }
  scene_["surfaceTransportVerified"] = true;
}
} // namespace twisty
