#include "twisty/session.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>

using namespace twisty;
namespace {
using Point = std::array<double, 3>;
constexpr double tolerance = 1e-8;
std::size_t checks = 0;
void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
Json read(const std::string &file) {
  std::ifstream input(std::string(PROJECT_ROOT) + "/" + file);
  return Json::parse(input);
}
Point difference(Point a, Point b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
Point unit(Point v) {
  const auto length = std::sqrt(dot(v, v));
  for (auto &component : v)
    component /= length;
  return v;
}
Point transformed(const Json &frame, Point point) {
  Point result{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      result[i] += frame.at(i * 3 + j).get<double>() * point[j];
  return result;
}
std::vector<Point> vertices(const Json &g, const Domain &domain, Index placement) {
  const auto &model = g.at("models").at(g.at("modelByDomain").at(domain.id).get<std::string>());
  const auto &frame = g.at("placementFrames").at(domain.placements[placement].key);
  std::vector<Point> points;
  for (const auto &v : model.at("vertices"))
    points.push_back(transformed(frame, v.get<Point>()));
  return points;
}
std::pair<double, double> projection(const std::vector<Point> &points, Point axis) {
  double low = std::numeric_limits<double>::infinity(), high = -low;
  for (const auto &p : points) {
    const auto value = dot(p, axis);
    low = std::min(low, value);
    high = std::max(high, value);
  }
  return {low, high};
}
void add_direction(std::vector<Point> &directions, Point direction) {
  if (dot(direction, direction) < tolerance * tolerance)
    return;
  direction = unit(direction);
  if (std::none_of(directions.begin(), directions.end(), [&](Point other) {
        return std::abs(dot(direction, other)) > 1 - tolerance * tolerance;
      }))
    directions.push_back(direction);
}
struct Hull {
  std::vector<Point> points, normals, edges;
  Point low, high;
  std::vector<Index> resources;
  explicit Hull(std::vector<Point> vertices, const Placement &placement)
      : points(std::move(vertices)), resources(placement.footprint_cells) {
    std::sort(resources.begin(), resources.end());
    for (int i = 0; i < 3; ++i) {
      Point axis{};
      axis[i] = 1;
      const auto range = projection(points, axis);
      low[i] = range.first;
      high[i] = range.second;
    }
    // Reconstruct supporting planes from exported vertices; do not import the
    // Python author's hull, cut, SAT or clique-cover implementation.
    std::vector<std::pair<Point, double>> planes;
    for (std::size_t a = 0; a < points.size(); ++a)
      for (std::size_t b = a + 1; b < points.size(); ++b)
        for (std::size_t c = b + 1; c < points.size(); ++c) {
          auto normal = cross(difference(points[b], points[a]), difference(points[c], points[a]));
          if (dot(normal, normal) < tolerance * tolerance)
            continue;
          normal = unit(normal);
          auto offset = dot(normal, points[a]);
          const auto range = projection(points, normal);
          if (range.second > offset + tolerance) {
            if (range.first < offset - tolerance)
              continue;
            for (auto &v : normal)
              v = -v;
            offset = -offset;
          }
          if (std::none_of(planes.begin(), planes.end(), [&](const auto &plane) {
                return dot(plane.first, normal) > 1 - tolerance &&
                       std::abs(plane.second - offset) < tolerance;
              }))
            planes.emplace_back(normal, offset);
        }
    check(planes.size() >= 4, "Exported piece has a closed three-dimensional hull");
    for (const auto &[normal, _] : planes)
      add_direction(normals, normal);
    for (std::size_t a = 0; a < points.size(); ++a)
      for (std::size_t b = a + 1; b < points.size(); ++b) {
        const auto shared = std::count_if(planes.begin(), planes.end(), [&](const auto &plane) {
          return std::abs(dot(plane.first, points[a]) - plane.second) < tolerance &&
                 std::abs(dot(plane.first, points[b]) - plane.second) < tolerance;
        });
        if (shared >= 2)
          add_direction(edges, difference(points[b], points[a]));
      }
  }
};
bool overlaps(const Hull &a, const Hull &b) {
  for (int i = 0; i < 3; ++i)
    if (a.high[i] <= b.low[i] + tolerance || b.high[i] <= a.low[i] + tolerance)
      return false;
  const auto separates = [&](Point axis) {
    if (dot(axis, axis) < tolerance * tolerance)
      return false;
    axis = unit(axis);
    const auto x = projection(a.points, axis), y = projection(b.points, axis);
    return x.second <= y.first + tolerance || y.second <= x.first + tolerance;
  };
  for (auto axis : a.normals)
    if (separates(axis))
      return false;
  for (auto axis : b.normals)
    if (separates(axis))
      return false;
  for (auto x : a.edges)
    for (auto y : b.edges)
      if (separates(cross(x, y)))
        return false;
  return true;
}
bool share_resource(const Hull &a, const Hull &b) {
  auto x = a.resources.begin(), y = b.resources.begin();
  while (x != a.resources.end() && y != b.resources.end()) {
    if (*x == *y)
      return true;
    if (*x < *y)
      ++x;
    else
      ++y;
  }
  return false;
}
void geometric_audit(const Definition &d, const Json &g, const Json &audit) {
  const std::map<std::string, Point> axes = {{"U", {0, 1, 0}}, {"D", {0, -1, 0}},
                                           {"R", {1, 0, 0}}, {"L", {-1, 0, 0}},
                                           {"F", {0, 0, 1}}, {"B", {0, 0, -1}}};
  std::map<std::string, Hull> locations;
  std::size_t guards = 0;
  const auto depth = audit.at("layerDepth").get<double>();
  for (Index di = 0; di < d.domains.size(); ++di) {
    const auto &domain = d.domains[di];
    for (Index q = 0; q < domain.placements.size(); ++q) {
      auto points = vertices(g, domain, q);
      const auto &placement = domain.placements[q];
      const auto location = std::find_if(placement.footprint.begin(), placement.footprint.end(),
                                         [](const auto &cell) { return cell.starts_with("location/"); });
      check(location != placement.footprint.end(), "Each placement has a location resource");
      const auto found = locations.find(*location);
      if (found == locations.end())
        locations.emplace(*location, Hull(points, placement));
      else {
        check(points.size() == found->second.points.size() &&
                  std::all_of(points.begin(), points.end(), [&](Point p) {
                    return std::any_of(found->second.points.begin(), found->second.points.end(),
                                       [&](Point other) {
                                         auto delta = difference(p, other);
                                         return dot(delta, delta) < tolerance * tolerance;
                                       });
                  }),
              "Placements sharing a location have the same unshrunk solid");
        auto resources = placement.footprint_cells;
        std::sort(resources.begin(), resources.end());
        check(resources == found->second.resources, "Shape aliases have identical exclusions");
      }
      for (const auto &op : d.operations) {
        const auto range = projection(points, axes.at(op.family));
        const int expected = range.first < depth - tolerance && range.second > depth + tolerance
                                 ? 2
                                 : (range.first >= depth - tolerance && range.second > depth + tolerance ? 1 : 0);
        check(op.roles[di][q] == expected, "Exported solid contradicts guard for " + op.id + "/" + placement.key);
        ++guards;
      }
    }
  }
  check(locations.size() == audit.at("distinctLocations").get<std::size_t>(), "Location catalog size");
  check(guards == audit.at("guardComparisons").get<std::size_t>(), "All directed cuts were audited");
  std::size_t pairs = 0, conflicts = 0;
  for (auto a = locations.begin(); a != locations.end(); ++a)
    for (auto b = std::next(a); b != locations.end(); ++b) {
      const bool overlap = overlaps(a->second, b->second);
      check(overlap == share_resource(a->second, b->second),
            "Exclusion resources contradict independent SAT at " + a->first + "/" + b->first);
      conflicts += overlap;
      ++pairs;
    }
  check(conflicts == audit.at("overlapPairs").get<std::size_t>(), "Overlap conflict count");
  std::cout << guards << " cut guards and " << pairs << " distinct-location pairs checked ("
            << conflicts << " overlaps)\n";
}
State run(const Definition &d, State state, const std::string &notation) {
  for (const auto &operation : parse_notation(d, notation)) {
    const auto next = plan(d, state, operation);
    check(next.transition.has_value(), "External sequence blocked at " + operation);
    state = next.transition->after;
  }
  return state;
}
Point port_center(const Definition &d, const Json &g, const Piece &piece, Index placement) {
  const auto &model = g.at("models").at(g.at("modelByDomain").at(piece.type).get<std::string>());
  Point center{};
  const auto &port = model.at("ports").at("0");
  for (const auto &index : port) {
    const auto v = model.at("vertices").at(index.get<Index>()).get<Point>();
    for (int k = 0; k < 3; ++k)
      center[k] += v[k] / port.size();
  }
  return transformed(g.at("placementFrames").at(d.domains[piece.domain].placements[placement].key), center);
}
void source_effects(const Definition &d, const Json &g, const Json &review) {
  for (const auto &fixture : review.at("pureKiteCycles")) {
    const auto state = run(d, d.initial, fixture.at("notation").get<std::string>());
    std::set<Index> moved;
    for (Index i = 0; i < d.pieces.size(); ++i)
      if (state.placement_of[i] != d.initial.placement_of[i]) {
        check(d.pieces[i].type.starts_with("kite-"), "Published pure cycle changed a non-kite");
        moved.insert(i);
      }
    check(moved.size() == 3, "Published pure cycle moves exactly three kites");
    auto current = *moved.begin();
    std::set<Index> visited;
    do {
      check(moved.contains(current) && visited.insert(current).second, "Kites form one three-cycle");
      const auto &piece = d.pieces[current];
      const auto match = std::find_if(d.pieces.begin(), d.pieces.end(), [&](const auto &target) {
        return target.domain == piece.domain && target.home == state.placement_of[current];
      });
      check(match != d.pieces.end(), "Kite destination is a solved-shaped slot");
      current = static_cast<Index>(match - d.pieces.begin());
    } while (current != *moved.begin());
    check(visited.size() == 3, "All moved kites belong to the same cycle");
    check(run(d, state, "(" + fixture.at("notation").get<std::string>() + ")'") == d.initial,
          "Published kite cycle reverses exactly");
  }
  const auto x = review.at("sliverPair").at("notation").get<std::string>();
  const auto forward = run(d, d.initial, x);
  // Parkin explicitly identifies the top-facing RU triangle and its RUB
  // destination. Identify the source by its surface region, not generated IDs.
  std::vector<Index> source;
  for (Index i = 0; i < d.pieces.size(); ++i) {
    const auto &piece = d.pieces[i];
    if (!piece.type.starts_with("triangle-"))
      continue;
    const auto p = port_center(d, g, piece, piece.home);
    if (std::abs(p[1] - 1) < tolerance && p[0] > 0.45 && std::abs(p[2]) < tolerance)
      source.push_back(i);
  }
  check(source.size() == 1, "One top-facing RU triangle is identified independently");
  const auto i = source.front();
  const auto destination = port_center(d, g, d.pieces[i], forward.placement_of[i]);
  check(std::abs(destination[1] - 1) < tolerance && destination[0] > 0 && destination[0] < 0.45 &&
            destination[2] < 0 && destination[2] > -0.45,
        "Parkin X sends the RU top triangle to the RUB corner triangle region");
  const auto inverse = run(d, d.initial, "(" + x + ")'");
  int protruding = 0;
  for (Index pi = 0; pi < d.pieces.size(); ++pi) {
    const auto &piece = d.pieces[pi];
    const auto points = vertices(g, d.domains[piece.domain], inverse.placement_of[pi]);
    const auto outside = std::any_of(points.begin(), points.end(), [](Point p) {
      return std::any_of(p.begin(), p.end(), [](double value) { return std::abs(value) > 1 + tolerance; });
    });
    if (!outside)
      continue;
    ++protruding;
    check(piece.type == "sliver", "Parkin inverse X has only the documented sliver protrusions");
    const auto xr = projection(points, {1, 0, 0}), yr = projection(points, {0, 1, 0});
    check(xr.first >= 0.45 - tolerance && xr.second > 1 && yr.second <= -0.45 + tolerance,
          "The pair lies on RD and protrudes through R, as required for Parkin X");
  }
  check(protruding == 2, "Exactly one sliver V-pair protrudes");
  check(run(d, inverse, x) == d.initial, "Parkin X resolves the RD pair");
}
} // namespace
int main() {
  try {
    const auto d = load_definition(read("packages/bagua/definition.json"));
    const auto g = read("packages/bagua/bagua-euclidean.json");
    const auto review = read("packages/bagua/review.json");
    check(d->digest == review.at("definitionDigest").get<std::string>(), "Review pins the current semantics");
    source_effects(*d, g, review);
    geometric_audit(*d, g, review.at("geometryAudit"));
    std::cout << checks << " independent Bagua review checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
