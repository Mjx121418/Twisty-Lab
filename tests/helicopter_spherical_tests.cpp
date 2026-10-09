#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>

using namespace twisty;
namespace {
using Point = std::array<double, 3>;
int checks = 0;
void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + path);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}
double dot(Point a, Point b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Point sub(Point a, Point b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
// Independent prototype membership equations, before cosmetic clearances.
bool contains(const std::string &model, Point p) {
  constexpr double margin = 1e-8;
  const auto [x, y, z] = p;
  if (model == "corner")
    return x + y > 1 + margin && x + z > 1 + margin && y + z > 1 + margin;
  if (model == "center")
    return x + y > 1 + margin && y + z > 1 + margin && x + z < 1 - margin;
  return x + y > 1 + margin && x + z < 1 - margin && x - z < 1 - margin &&
         y + z < 1 - margin && y - z < 1 - margin;
}
Point world(const Geometry &g, const Json &asset, const std::vector<float> &frames, std::size_t part,
            std::size_t vertex) {
  const auto offset = asset.at("positionOffset").get<std::size_t>() + vertex * 3;
  Point p{};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col)
      p[row] += frames[part * 16 + col * 4 + row] * g.positions()[offset + col];
  return p;
}
void run() {
  Session session(read("packages/helicopter/definition.json"));
  const auto d = session.definition();
  const auto package = Json::parse(read("packages/helicopter/helicopter-spherical.json"));
  Geometry g(d, package.dump());
  const auto scene = Json::parse(g.scene_json());
  const double radius = package.at("radius"), lift = package.at("surfaceLift");
  check(d->pieces.size() == 44 && scene.at("visualParts").size() == 92 && scene.at("meshAssets").size() == 7,
        "Sphere reuses all 44 existing pieces and 48 ports with seven shared assets");
  check(scene.at("ambientSpace") == "S2" && scene.at("diskCenters").size() == 12 &&
            scene.at("catalogTransportVerified") == true && scene.at("surfaceTransportVerified") == true,
        "Twelve spherical disks verify catalog and surface transports");
  check(scene.at("fidelity") == "spherical-section-with-inherited-guards", "Declare inherited guard scope");
  check(scene.at("definitionDigest").get<std::string>() == d->digest, "Reuse exact abstract semantics");
  std::map<std::string, Json> assets;
  std::map<std::string, Index> pieces;
  std::vector<std::size_t> body_parts;
  for (Index i = 0; i < d->pieces.size(); ++i)
    pieces[d->pieces[i].id] = i;
  for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i) {
    const auto &part = scene.at("visualParts")[i];
    const auto hit = Json::parse(g.bind_hit_json(part.at("visualPartId")));
    check(hit.at("pieceId") == part.at("pieceId"), "Picking preserves persistent identity");
    if (part.at("role") == "body")
      body_parts.push_back(i);
    else
      check(hit.at("portId") == part.at("portId"), "Picking preserves the port binding");
  }
  std::vector<std::size_t> edge_permutation;
  for (const auto &asset : scene.at("meshAssets")) {
    const auto id = asset.at("id").get<std::string>();
    assets[id] = asset;
    const auto offset = asset.at("positionOffset").get<std::size_t>();
    const auto count = asset.at("vertexCount").get<std::size_t>();
    const double shell = id.ends_with("/body") ? 1 - lift / radius : 1;
    auto local = [&](std::size_t v) { return Point{g.positions()[offset + v * 3], g.positions()[offset + v * 3 + 1], g.positions()[offset + v * 3 + 2]}; };
    for (std::size_t v = 0; v < count; ++v) {
      const auto p = local(v);
      const Point n{g.normals()[offset + v * 3], g.normals()[offset + v * 3 + 1], g.normals()[offset + v * 3 + 2]};
      check(std::abs(dot(p, p) - shell * shell) < 2e-7 && std::abs(dot(n, n) - 1) < 2e-7 &&
                std::abs(dot(p, n) - shell) < 2e-7, "Radial shells and normals are correct");
      if (id == "edge/body") {
        const Point swapped{p[1], p[0], -p[2]};
        auto nearest = count;
        for (std::size_t w = 0; w < count; ++w) {
          const auto delta = sub(swapped, local(w));
          if (dot(delta, delta) < 1e-14) { nearest = w; break; }
        }
        check(nearest < count, "Uncolored edge mesh preserves the catalog's half-turn symmetry");
        edge_permutation.push_back(nearest);
      }
    }
    const auto index_offset = asset.at("indexOffset").get<std::size_t>();
    for (std::size_t j = 0; j < asset.at("indexCount").get<std::size_t>(); j += 3) {
      const auto a = g.indices()[index_offset + j], b = g.indices()[index_offset + j + 1], c = g.indices()[index_offset + j + 2];
      check(a < count && b < count && c < count, "Indices stay within the shared asset");
      const auto pa = local(a), pb = local(b), pc = local(c);
      check(dot(cross(sub(pb, pa), sub(pc, pa)), pa) > 0, "Triangle winding is outward and nondegenerate");
      if (!id.ends_with("/body")) {
        const double minimum = std::min({dot(pa, pa), dot(pb, pb), dot(pc, pc)});
        const double pair = std::min({minimum, dot(pa, pb), dot(pa, pc), dot(pb, pc)});
        check((minimum + 2 * pair) / 3 > (1 - lift / radius) * (1 - lift / radius),
              "Colored triangle faces remain outside the backing shell");
      }
    }
  }
  auto coverage = [&](const std::vector<float> &frames) {
    for (int i = 0; i < 512; ++i) {
      const double z = 1 - 2 * (i + 0.5) / 512, angle = i * std::numbers::pi * (3 - std::sqrt(5.0));
      const Point p{std::sqrt(1 - z * z) * std::cos(angle), std::sqrt(1 - z * z) * std::sin(angle), z};
      int count = 0;
      for (auto part : body_parts) {
        const auto id = scene.at("visualParts")[part].at("meshAssetId").get<std::string>();
        Point local{};
        for (int col = 0; col < 3; ++col)
          for (int row = 0; row < 3; ++row)
            local[col] += frames[part * 16 + col * 4 + row] * p[row] / radius;
        count += contains(id.substr(0, id.find('/')), local);
      }
      check(count == 1, "Spherical sections cover each probe exactly once, including during jumbling");
    }
  };
  auto verify = [&](const Transition &transition) {
    g.set_state_json(encode_state(*d, transition.before).dump());
    const auto source = g.transforms();
    check(Json::parse(g.prepare_animation_json(encode_transition(*d, transition).dump())).at("status") == "Prepared",
          "Accept verified abstract transitions");
    check(g.transforms() == source, "Animation starts at the exact source");
    g.sample(0.5);
    const auto middle = g.transforms();
    coverage(middle);
    g.sample(1 - 1e-7);
    const auto near = g.transforms();
    g.sample(1);
    const auto target = g.transforms();
    g.set_state_json(encode_state(*d, transition.after).dump());
    check(target == g.transforms(), "Animation endpoint matches independently reconstructed state");
    coverage(target);
    const auto &operation = d->operations[d->operation_ids.at(transition.operation)];
    for (std::size_t part = 0; part < scene.at("visualParts").size(); ++part) {
      const auto &binding = scene.at("visualParts")[part];
      const auto pi = pieces.at(binding.at("pieceId"));
      if (operation.roles[d->pieces[pi].domain][transition.before.placement_of[pi]] != 1)
        for (int k = 0; k < 16; ++k)
          check(source[part * 16 + k] == middle[part * 16 + k], "Stationary pieces remain fixed");
      const auto id = binding.at("meshAssetId").get<std::string>();
      const auto &asset = assets.at(id);
      for (std::size_t v = 0; v < asset.at("vertexCount").get<std::size_t>(); ++v) {
        const auto actual = world(g, asset, near, part, v);
        auto delta = sub(actual, world(g, asset, target, part, v));
        double distance = dot(delta, delta);
        if (id == "edge/body") {
          delta = sub(actual, world(g, asset, target, part, edge_permutation[v]));
          distance = std::min(distance, dot(delta, delta));
        }
        check(distance < 2e-12, "Continuous motion reaches the identical mesh before canonicalization");
        const auto p = world(g, asset, middle, part, v), original = world(g, asset, source, part, v);
        check(std::abs(dot(p, p) - dot(original, original)) < 2e-6, "Intermediate motion remains on S2");
      }
    }
    check(plan(*d, transition.after, operation.inverse).transition->after == transition.before, "Inverse is exact");
  };
  const auto saved = session.save();
  coverage(g.transforms());
  auto state = d->initial;
  for (int step = 0; step < 4; ++step) {
    for (const auto &operation : d->operations) {
      const auto result = plan(*d, state, operation.id);
      if (result.transition)
        verify(*result.transition);
    }
    const auto requests = legal_operations(*d, state);
    state = plan(*d, state, requests[(step * 11 + 5) % requests.size()].at("operation")).transition->after;
  }
  check(session.save() == saved, "Geometry leaves session and history untouched");
  check(session.run("UF_ab UR_ad", "transactional", "0").at("reasonCode") == "placement.blocked",
        "Spherical view retains the core's volume and catalog blocking rules");
  check(session.state() == d->initial, "Blocked algorithm preserves the exact state");
  for (const std::string key : {"diskAngleDegrees", "scale", "angularSegments"}) {
    auto bad = package;
    bad[key] = key == "diskAngleDegrees" ? 50 : key == "scale" ? 1 : 15;
    bool rejected = false;
    try { Geometry invalid(d, bad.dump()); }
    catch (const DiagnosticError &e) { rejected = e.code == "realization.spherical"; }
    check(rejected, "Reject incompatible spherical dimensions and tessellation");
  }
}
} // namespace
int main() {
  try { run(); std::cout << checks << " Helicopter spherical checks passed\n"; }
  catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
