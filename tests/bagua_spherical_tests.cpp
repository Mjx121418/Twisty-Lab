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
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point sub(Point a, Point b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
// Independent region equations: do not derive coverage from exported hulls
// or from the interpreter's tessellation. Ignore cosmetic sticker insets.
bool contains(const std::string &model, Point p) {
  constexpr double d = 81.0 / 200, margin = 1e-8;
  const double c = std::sqrt(2.0) * d;
  const auto [x, y, z] = p;
  if (model == "corner")
    return x > d + margin && y > d + margin && z > d + margin;
  if (model == "center" || model == "triangle-center") {
    if (!(y > d + margin && std::abs(x) < d - margin && std::abs(z) < d - margin))
      return false;
    return model == "center" ? std::abs(x + z) < c - margin && std::abs(x - z) < c - margin
                             : x + z > c + margin;
  }
  if (!(x > d + margin && z > d + margin && std::abs(y) < d - margin))
    return false;
  if (model == "edge")
    return x - std::abs(y) > c + margin && z - std::abs(y) > c + margin;
  if (model == "triangle-edge")
    return x - std::abs(y) > c + margin && z + std::abs(y) < c - margin;
  if (model == "kite-left")
    return x - y > c + margin && z + y > c + margin && z - y < c - margin;
  if (model == "kite-right")
    return x + y > c + margin && z - y > c + margin && z + y < c - margin;
  return x - y < c - margin && z - y < c - margin && x + y > c + margin && z + y > c + margin;
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
  Session session(read("packages/bagua/definition.json"));
  const auto d = session.definition();
  const auto package = Json::parse(read("packages/bagua/bagua-spherical.json"));
  const auto original = Json::parse(read("packages/bagua/bagua-euclidean.json"));
  Geometry g(d, package.dump());
  const auto scene = Json::parse(g.scene_json());
  const double radius = package.at("radius"), lift = package.at("surfaceLift");
  check(d->pieces.size() == 146 && scene.at("visualParts").size() == 344 &&
            scene.at("meshAssets").size() == 20,
        "Sphere reuses 146 pieces and 198 ports with twenty shared assets");
  check(scene.at("ambientSpace") == "S2" && scene.at("diskCenters").size() == 6 &&
            scene.at("catalogTransportVerified") == true && scene.at("surfaceTransportVerified") == true,
        "Six disks verify every catalog and continuous surface transport");
  check(std::abs(std::cos(package.at("diskAngleDegrees").get<double>() * std::numbers::pi / 180) -
                 81.0 / 200) < 1e-12,
        "Disk depth is 81/200");
  check(scene.at("fidelity") == "spherical-section-with-inherited-guards" &&
            scene.at("definitionDigest").get<std::string>() == d->digest,
        "Declare unchanged semantics and inherited guards");
  for (const auto &[name, model] : package.at("models").items()) {
    auto port_model = model;
    port_model.erase("surfaceVertices");
    check(port_model == original.at("models").at(name),
          "Surface prototypes preserve original port geometry and identity");
  }
  std::map<std::string, Json> assets;
  std::map<std::string, Index> pieces;
  struct Body {
    std::size_t part;
    std::string model;
  };
  std::vector<Body> bodies;
  for (Index i = 0; i < d->pieces.size(); ++i)
    pieces[d->pieces[i].id] = i;
  for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i) {
    const auto &part = scene.at("visualParts")[i];
    const auto hit = Json::parse(g.bind_hit_json(part.at("visualPartId")));
    check(hit.at("pieceId") == part.at("pieceId"), "Picking preserves persistent piece identity");
    if (part.at("role") == "body") {
      const auto id = part.at("meshAssetId").get<std::string>();
      bodies.push_back({i, id.substr(0, id.find('/'))});
    } else
      check(hit.at("portId") == part.at("portId"), "Picking preserves local port identity");
  }
  check(bodies.size() == 146, "Every piece has a surface region");
  for (const auto &asset : scene.at("meshAssets")) {
    const auto id = asset.at("id").get<std::string>();
    assets[id] = asset;
    const auto offset = asset.at("positionOffset").get<std::size_t>();
    const auto count = asset.at("vertexCount").get<std::size_t>();
    const double shell = id.ends_with("/body") ? 1 - lift / radius : 1;
    auto local = [&](std::size_t v) {
      return Point{g.positions()[offset + v * 3], g.positions()[offset + v * 3 + 1],
                   g.positions()[offset + v * 3 + 2]};
    };
    for (std::size_t v = 0; v < count; ++v) {
      const auto p = local(v);
      const Point n{g.normals()[offset + v * 3], g.normals()[offset + v * 3 + 1],
                    g.normals()[offset + v * 3 + 2]};
      check(std::abs(dot(p, p) - shell * shell) < 2e-7 && std::abs(dot(n, n) - 1) < 2e-7 &&
                std::abs(dot(p, n) - shell) < 2e-7,
            "Meshes have radial normals on the declared shells");
    }
    const auto index_offset = asset.at("indexOffset").get<std::size_t>();
    for (std::size_t j = 0; j < asset.at("indexCount").get<std::size_t>(); j += 3) {
      const auto a = g.indices()[index_offset + j], b = g.indices()[index_offset + j + 1],
                 c = g.indices()[index_offset + j + 2];
      check(a < count && b < count && c < count, "Indices stay within their shared asset");
      const auto pa = local(a), pb = local(b), pc = local(c);
      check(dot(cross(sub(pb, pa), sub(pc, pa)), pa) > 0, "Triangle winding is outward and nondegenerate");
      if (!id.ends_with("/body")) {
        const double minimum = std::min({dot(pa, pa), dot(pb, pb), dot(pc, pc)});
        const double pair = std::min({minimum, dot(pa, pb), dot(pa, pc), dot(pb, pc)});
        check((minimum + 2 * pair) / 3 > (1 - lift / radius) * (1 - lift / radius),
              "Colored triangles stay outside the dark backing shell");
      }
    }
  }
  auto coverage = [&](const std::vector<float> &frames, int probes = 512) {
    for (int i = 0; i < probes; ++i) {
      const double z = 1 - 2 * (i + 0.5) / probes, angle = i * std::numbers::pi * (3 - std::sqrt(5.0));
      const Point p{std::sqrt(1 - z * z) * std::cos(angle), std::sqrt(1 - z * z) * std::sin(angle), z};
      int count = 0;
      for (const auto &body : bodies) {
        Point local{};
        for (int col = 0; col < 3; ++col)
          for (int row = 0; row < 3; ++row)
            local[col] += frames[body.part * 16 + col * 4 + row] * p[row] / radius;
        count += contains(body.model, local);
      }
      check(count == 1, "Spherical regions cover each probe exactly once during legal jumbling");
    }
  };
  auto verify = [&](const Transition &t) {
    g.set_state_json(encode_state(*d, t.before).dump());
    const auto source = g.transforms();
    check(Json::parse(g.prepare_animation_json(encode_transition(*d, t).dump())).at("status") == "Prepared",
          "Prepare exact core transitions");
    check(g.transforms() == source, "Animation starts at the exact source");
    g.sample(0.5);
    const auto middle = g.transforms();
    coverage(middle);
    g.sample(1 - 1e-7);
    const auto near = g.transforms();
    g.sample(1);
    const auto target = g.transforms();
    g.set_state_json(encode_state(*d, t.after).dump());
    check(target == g.transforms(), "Animation endpoint equals the independently reconstructed state");
    coverage(target);
    const auto &op = d->operations[d->operation_ids.at(t.operation)];
    for (std::size_t part = 0; part < scene.at("visualParts").size(); ++part) {
      const auto &binding = scene.at("visualParts")[part];
      const auto pi = pieces.at(binding.at("pieceId"));
      if (op.roles[d->pieces[pi].domain][t.before.placement_of[pi]] != 1)
        for (int k = 0; k < 16; ++k)
          check(source[part * 16 + k] == middle[part * 16 + k], "Stationary pieces remain fixed");
      const auto &asset = assets.at(binding.at("meshAssetId").get<std::string>());
      for (std::size_t v = 0; v < asset.at("vertexCount").get<std::size_t>(); ++v) {
        const auto delta = sub(world(g, asset, near, part, v), world(g, asset, target, part, v));
        check(dot(delta, delta) < 2e-12,
              "Continuous motion reaches the identical mesh before canonicalization");
        const auto p = world(g, asset, middle, part, v), q = world(g, asset, source, part, v);
        check(std::abs(dot(p, p) - dot(q, q)) < 2e-6, "Intermediate motion preserves S2");
      }
    }
    check(plan(*d, t.after, op.inverse).transition->after == t.before, "Inverse is exact");
  };
  const auto saved = session.save();
  coverage(g.transforms(), 2048);
  auto state = d->initial;
  for (const std::string step : {"U+", "R", "F-", "F+"}) {
    for (const auto &op : d->operations) {
      const auto result = plan(*d, state, op.id);
      if (result.transition)
        verify(*result.transition);
    }
    const auto result = plan(*d, state, step);
    check(result.transition.has_value(), "Jumbled coverage fixture stays legal");
    state = result.transition->after;
  }
  check(session.save() == saved, "Geometry leaves the session and history untouched");
  check(session.run("U+ R F- U+", "transactional", "0").at("reasonCode") == "placement.blocked" &&
            session.state() == d->initial,
        "Spherical presentation retains original blocking and rollback");
  auto reject = [&](Json bad, const std::string &code) {
    bool rejected = false;
    try {
      Geometry invalid(d, bad.dump());
    } catch (const DiagnosticError &e) {
      rejected = e.code == code;
    }
    check(rejected, "Reject incompatible surface metadata: " + code);
  };
  auto bad = package;
  bad["diskAngleDegrees"] = 65;
  reject(bad, "realization.spherical");
  bad = package;
  bad["models"]["center"]["surfaceVertices"] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  reject(bad, "realization.catalog");
  bad = package;
  bad["models"]["center"]["surfaceVertices"][0].push_back(0);
  reject(bad, "realization.catalog");
  bad = package;
  bad["models"]["center"]["surfaceVertices"][0][0] = -0.39;
  const double s = std::sqrt(0.5);
  bad["models"]["center"]["symmetries"].push_back({s, 0, s, 0, 1, 0, -s, 0, s});
  reject(bad, "realization.catalog");
}
} // namespace
int main() {
  try {
    run();
    std::cout << checks << " Bagua spherical checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
