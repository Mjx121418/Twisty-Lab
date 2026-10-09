#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>

using namespace twisty;
namespace {
using Point = std::array<double, 3>;
int checks = 0;
void check(bool result, const std::string &message) {
  ++checks;
  if (!result)
    throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream input(std::string(PROJECT_ROOT) + "/" + path);
  if (!input)
    throw std::runtime_error("Cannot read " + path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point difference(Point a, Point b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Point normal(const std::string &face) {
  static const std::map<std::string, Point> normals{{"U", {0, 1, 0}},  {"R", {1, 0, 0}},  {"F", {0, 0, 1}},
                                                    {"D", {0, -1, 0}}, {"L", {-1, 0, 0}}, {"B", {0, 0, -1}}};
  return normals.at(face);
}
Point point(const Geometry &g, const Json &asset, const std::vector<float> &frames, std::size_t part,
            std::size_t vertex) {
  const auto offset = asset.at("positionOffset").get<std::size_t>() + vertex * 3;
  Point result{};
  for (int row = 0; row < 3; ++row) {
    result[row] = frames[part * 16 + 12 + row];
    for (int column = 0; column < 3; ++column)
      result[row] += frames[part * 16 + column * 4 + row] * g.positions()[offset + column];
  }
  return result;
}
void test_package(const std::string &name) {
  Session session(read("packages/" + name + "/definition.json"));
  const auto d = session.definition();
  const auto package = Json::parse(read("packages/" + name + "/cube-spherical.json"));
  Geometry g(d, package.dump());
  const auto scene = Json::parse(g.scene_json());
  const auto untouched = session.save();
  const double radius = package.at("radius"), lift = package.at("surfaceLift");
  const double threshold = std::cos(package.at("diskAngleDegrees").get<double>() * std::numbers::pi / 180);
  check(scene.at("ambientSpace") == "S2" && scene.at("surfaceTransportVerified") == true,
        "Sphere declares its ambient space and verified port transport");
  check(scene.at("definitionDigest").get<std::string>() == d->digest, "Sphere pins existing cube semantics");
  check(scene.at("visualParts").size() == 108 && scene.at("meshAssets").size() == 8,
        "54 labeled ports share four body and four port assets");
  std::map<std::string, Json> assets;
  std::map<std::string, Index> pieces;
  for (Index i = 0; i < d->pieces.size(); ++i)
    pieces[d->pieces[i].id] = i;
  for (const auto &asset : scene.at("meshAssets")) {
    const auto id = asset.at("id").get<std::string>();
    assets[id] = asset;
    const auto offset = asset.at("positionOffset").get<std::size_t>();
    const auto count = asset.at("vertexCount").get<std::size_t>();
    const double expected = id.ends_with("/port") ? radius : radius - lift;
    for (std::size_t i = 0; i < count; ++i) {
      Point p{}, n{};
      for (int row = 0; row < 3; ++row) {
        p[row] = g.positions()[offset + i * 3 + row];
        n[row] = g.normals()[offset + i * 3 + row];
      }
      check(std::abs(std::sqrt(dot(p, p)) - expected) < 3e-7,
            "Every vertex lies on its specified spherical shell");
      check(std::abs(dot(n, n) - 1) < 2e-7 && std::abs(dot(p, n) - expected) < 4e-7,
            "Normals point radially outward");
    }
    const auto index_offset = asset.at("indexOffset").get<std::size_t>();
    const auto index_count = asset.at("indexCount").get<std::size_t>();
    for (std::size_t i = 0; i < index_count; i += 3) {
      Point a{}, b{}, c{};
      std::array<Point *, 3> targets{&a, &b, &c};
      for (int j = 0; j < 3; ++j) {
        const auto v = g.indices()[index_offset + i + j];
        check(v < count, "Triangle indices are local to their mesh asset");
        for (int row = 0; row < 3; ++row)
          (*targets[j])[row] = g.positions()[offset + v * 3 + row];
      }
      const auto outward = cross(difference(b, a), difference(c, a));
      if (id.ends_with("/port")) {
        // For convex weights, sum(w_i^2) >= 1/3. This bounds the
        // radius of every point inside the triangle, including its edges.
        const double min_norm = std::min({dot(a, a), dot(b, b), dot(c, c)});
        const double min_pair = std::min({min_norm, dot(a, b), dot(a, c), dot(b, c)});
        check((min_norm + 2 * min_pair) / 3 > (radius - lift) * (radius - lift),
              "Colored triangle faces remain outside the entire backing sphere");
      }
      check(dot(outward, a) > 0, "Every triangle is nondegenerate and faces outward");
    }
  }
  auto check_resting = [&](const State &state) {
    g.set_state_json(encode_state(*d, state).dump());
    std::map<std::set<std::string>, std::set<std::string>> coverage;
    for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i) {
      const auto &part = scene.at("visualParts")[i];
      const auto &piece = d->pieces[pieces.at(part.at("pieceId"))];
      const auto &placement = d->domains[piece.domain].placements[state.placement_of[pieces.at(piece.id)]];
      const auto attachment = placement.ports.at(part.at("portId"));
      std::set<std::string> faces;
      for (char face : attachment.first)
        faces.insert(std::string(1, face));
      if (part.at("role") == "port")
        coverage[faces].insert(attachment.second);
      const auto &asset = assets.at(part.at("meshAssetId"));
      const double shell = part.at("role") == "port" ? radius : radius - lift;
      for (std::size_t v = 0; v < asset.at("vertexCount").get<std::size_t>(); ++v) {
        auto p = point(g, asset, g.transforms(), i, v);
        for (auto &value : p)
          value /= shell;
        const double dominant = dot(p, normal(attachment.second));
        for (const auto &face : {"U", "R", "F", "D", "L", "B"}) {
          const double coordinate = dot(p, normal(face));
          check(faces.contains(face) ? coordinate >= threshold - 3e-7 : coordinate <= threshold + 3e-7,
                "Region agrees with every disk membership in the exact placement");
          check(dominant >= coordinate - 3e-7, "Colored port stays in its face's dominant region");
        }
      }
      const auto hit = Json::parse(g.bind_hit_json(part.at("visualPartId")));
      check(hit.at("pieceId") == part.at("pieceId") && hit.at("portId") == part.at("portId"),
            "Surface picking returns the persistent piece and port");
    }
    check(coverage.size() == 26, "All 26 geometric regions are occupied");
    for (const auto &[faces, ports] : coverage)
      check(faces == ports, "Every region has exactly its incident face ports");
  };
  auto verify = [&](const Transition &transition) {
    check_resting(transition.before);
    const auto source = g.transforms();
    const auto prepared = Json::parse(g.prepare_animation_json(encode_transition(*d, transition).dump()));
    check(prepared.at("status") == "Prepared" && g.transforms() == source,
          "Disk turn begins at the exact source frame");
    g.sample(0.5);
    const auto middle = g.transforms();
    g.sample(1 - 1e-7);
    const auto near_target = g.transforms();
    g.sample(1);
    const auto target = g.transforms();
    check_resting(transition.after);
    check(target == g.transforms(), "Animation endpoint equals independently reconstructed target");
    for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i) {
      const auto &part = scene.at("visualParts")[i];
      const auto &piece = d->pieces[pieces.at(part.at("pieceId"))];
      const auto &placement =
          d->domains[piece.domain].placements[transition.before.placement_of[pieces.at(piece.id)]];
      const auto &op = d->operations[d->operation_ids.at(transition.operation)];
      const bool moving =
          std::all_of(placement.footprint.begin(), placement.footprint.end(), [&](const auto &cell) {
            return std::find(op.cells.begin(), op.cells.end(), cell) != op.cells.end();
          });
      if (!moving)
        for (int k = 0; k < 16; ++k)
          check(middle[i * 16 + k] == source[i * 16 + k], "A stationary surface has no intermediate motion");
      const auto &asset = assets.at(part.at("meshAssetId"));
      const auto count = asset.at("vertexCount").get<std::size_t>();
      const bool symmetric = part.at("meshAssetId").get<std::string>().starts_with("center/");
      for (std::size_t v = 0; v < count; ++v) {
        const auto actual = point(g, asset, near_target, i, v);
        auto delta = difference(actual, point(g, asset, target, i, v));
        double distance = dot(delta, delta);
        if (symmetric && distance > 1e-12)
          for (std::size_t w = 0; w < count; ++w) {
            delta = difference(actual, point(g, asset, target, i, w));
            distance = std::min(distance, dot(delta, delta));
          }
        check(distance < 1e-12,
              "Continuous disk rotation reaches the mesh before canonicalization, including center symmetry");
        const auto midway = point(g, asset, middle, i, v);
        const auto initial = point(g, asset, source, i, v);
        check(std::abs(dot(midway, midway) - dot(initial, initial)) < 2e-6,
              "Intermediate motion stays on the spherical shell");
      }
    }
    const auto inverse =
        plan(*d, transition.after, d->operations[d->operation_ids.at(transition.operation)].inverse);
    check(inverse.transition && inverse.transition->after == transition.before,
          "Inverse restores the identical abstract state");
  };
  check_resting(d->initial);
  auto state = d->initial;
  for (int step = 0; step < 4; ++step) {
    for (const auto &op : d->operations) {
      const auto result = plan(*d, state, op.id);
      if (result.transition)
        verify(*result.transition);
    }
    const auto requests = legal_operations(*d, state);
    state = plan(*d, state, requests[(step * 7 + 3) % requests.size()].at("operation")).transition->after;
  }
  if (name == "bandaged")
    check(plan(*d, d->initial, "R").blocked.at("reasonCode") == "footprint.partial_overlap",
          "Sphere retains core bandage blocking");
  check(session.save() == untouched, "Sampling geometry never edits the session, history, or revision");
  for (const double angle : {45.0, 90.0, 420.0}) {
    auto invalid = package;
    invalid["diskAngleDegrees"] = angle;
    bool rejected = false;
    try {
      Geometry bad(d, invalid.dump());
    } catch (const DiagnosticError &e) {
      rejected = e.code == "realization.spherical";
    }
    check(rejected, "Reject disk arrangements without cube topology");
  }
  auto invalid = package;
  invalid["angularSegments"] = 15;
  bool rejected = false;
  try {
    Geometry bad(d, invalid.dump());
  } catch (const DiagnosticError &e) {
    rejected = e.code == "realization.spherical";
  }
  check(rejected, "Reject an unsupported tessellation");
  auto partial_disk = Json::parse(session.definition_json());
  partial_disk.erase("definitionDigest");
  for (auto &operation : partial_disk["operations"])
    if (operation["family"] == "U")
      operation["selectedCells"] = {"U"};
  // Turning only the featureless center is a valid abstract operation, but
  // does not rotate the whole spherical cap that this realization promises.
  const auto changed = load_definition(partial_disk);
  auto pinned = package;
  pinned["compatibleDefinitionDigest"] = changed->digest;
  rejected = false;
  try {
    Geometry bad(changed, pinned.dump());
  } catch (const DiagnosticError &e) {
    rejected = e.code == "realization.spherical";
  }
  check(rejected, "Reject abstract moves that select only part of a spherical disk");
}
} // namespace
int main() {
  try {
    test_package("cube3");
    test_package("bandaged");
    std::cout << checks << " spherical checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
