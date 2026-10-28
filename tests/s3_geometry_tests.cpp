#include "twisty/kernel.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace twisty;
namespace {
void check(bool b, const std::string &why) {
  if (!b)
    throw std::runtime_error(why);
}
std::string read(const std::string &p) {
  std::ifstream f(std::string(PROJECT_ROOT) + "/" + p);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}
template <class F> void reject(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("Expected rejection");
}
void orthogonal(const GeometryFrame &f) {
  auto &m = f.isometries();
  for (size_t s = 0; s < m.size(); s += 16)
    for (int a = 0; a < 4; a++)
      for (int b = 0; b < 4; b++) {
        double d = 0;
        for (int i = 0; i < 4; i++)
          d += m[s + 4 * a + i] * m[s + 4 * b + i];
        check(std::abs(d - (a == b)) < 1e-8, "Non-SO4 frame");
      }
}
void finite(const GeometryDrawing &d, double radius = 8) {
  const auto &p = d.positions(), &n = d.normals();
  check(!p.empty() && p.size() == n.size(), "Missing drawing");
  for (size_t i = 0; i < p.size(); i += 3) {
    double r = 0, nn = 0;
    for (int j = 0; j < 3; j++) {
      check(std::isfinite(p[i + j]) && std::isfinite(n[i + j]), "Nonfinite projection");
      r += p[i + j] * p[i + j];
      nn += n[i + j] * n[i + j];
    }
    check(r <= radius * radius + 1e-4 && std::abs(nn - 1) < 1e-5, "Projection bounds/normals");
  }
  const auto scene = Json::parse(d.scene_json());
  for (const auto &a : scene.at("meshAssets"))
    for (size_t i = a.at("indexOffset");
         i < a.at("indexOffset").get<size_t>() + a.at("indexCount").get<size_t>(); i++)
      check(d.indices().at(i) < a.at("vertexCount"), "Mesh-local index out of bounds");
}
} // namespace
int main() {
  try {
    Session session(read("packages/tesseract3/package.json"));
    session.lightweight_results(true);
    const auto r = Json::parse(read("packages/tesseract3/tesseract3-s3.json"));
    GeometryDefinition g(session.execution(), r.dump());
    const auto intrinsic = g.intrinsic_mesh();
    const auto intrinsic_descriptor = Json::parse(intrinsic.descriptor_json());
    check(intrinsic_descriptor.at("version") == 1 && intrinsic_descriptor.at("coordinateSize") == 4,
          "Wrong intrinsic mesh buffer version/layout");
    check(intrinsic_descriptor.at("visualParts").size() == 216 &&
              intrinsic_descriptor.at("meshAssets").size() == 10,
          "Intrinsic prototypes are not shared");
    check(intrinsic.positions().size() == 4840 * 4 &&
              intrinsic.positions().size() == intrinsic.normals().size(), "Wrong intrinsic buffer size");
    for (const auto &part : intrinsic_descriptor.at("visualParts")) {
      const auto center = part.at("boundingCap").at("center").get<std::vector<double>>();
      const auto cosine = part.at("boundingCap").at("minDot").get<double>();
      check(cosine > 0, "Nonconvex cap");
      for (const auto &asset : intrinsic_descriptor.at("meshAssets")) if (asset.at("id") == part.at("meshAssetId")) {
        const auto offset = asset.at("positionOffset").get<std::size_t>();
        for (std::size_t v = 0; v < asset.at("vertexCount"); ++v) {
          double value = 0;
          for (int j = 0; j < 4; ++j) value += center[j] * intrinsic.positions()[offset + 4 * v + j];
          check(value >= cosine - 1e-7, "Intrinsic cap misses a vertex");
        }
      }
    }
    const auto initial = session.state_json();
    auto frame = g.frame(initial);
    orthogonal(frame);
    auto drawing = g.project(frame, "stereographic");
    finite(drawing);
    const auto saved = drawing.positions();
    const auto depths = Json::parse(drawing.scene_json()).at("projection").at("pieceDepths");
    check(depths.size() == 80, "Missing projection depths");
    int outer = 0;
    for (const auto &depth : depths)
      outer += depth.get<double>() > .4;
    check(outer == 27, "Outer projected layer must contain 27 pieces");
    check(Json::parse(drawing.scene_json()).at("visualParts").size() == 216, "216 color sectors");
    const auto &definition = *session.execution()->definition();
    check(definition.pieces.size() == 80 && definition.operations.size() == 184, "Wrong puzzle size");
    size_t tested = 0;
    for (const auto &op : definition.operations) {
      const auto result = session.execute(op.id, session.revision());
      check(result.at("status") == "Committed", op.id + " unavailable");
      const auto t = result.at("transitions").at(0);
      check(t.at("pieceActions").size() == 27, "Incomplete cap");
      auto a = g.animate(t.dump());
      orthogonal(a.evaluate(.37));
      check(a.evaluate(1).isometries() == g.frame(t.at("afterState").dump()).isometries(), "Endpoint frame");
      if (tested++ % 23 == 0) {
        auto middle = g.project(a.evaluate(.37), "stereographic");
        finite(middle);
        check(middle.positions() != saved, "Projection is not deforming");
      }
      session.execute(op.inverse, session.revision());
      check(session.state_json() == initial, "Inverse failed");
    }
    check(session.scramble(2026, 24, session.revision()).at("status") == "Committed", "Scramble failed");
    const auto scrambled = session.state_json();
    for (const auto &op : definition.operations) {
      const auto result = session.execute(op.id, session.revision());
      check(result.at("status") == "Committed" && result.at("transitions")[0]["pieceActions"].size() == 27,
            "Scrambled cap is unavailable");
      session.execute(op.inverse, session.revision());
      check(session.state_json() == scrambled, "Scrambled inverse failed");
    }
    for (size_t i = 0; i < 2 * definition.operations.size() + 24; ++i)
      session.undo(session.revision());
    check(session.state_json() == initial, "Scramble undo failed");
    for (const auto &group : r.at("controlLayout").at("groups")) {
      auto options =
          Json{{"focusCell", group.at("id")},
               {"guide", {{"group", group.at("id")}, {"axis", group.at("categories")[0]["axes"][0]["id"]}}}};
      auto d = g.project_with_options(frame, "stereographic", options.dump());
      finite(d);
      const auto focus_depths = Json::parse(d.scene_json()).at("projection").at("pieceDepths");
      check(std::abs(focus_depths.at("center/" + group.at("id").get<std::string>()).get<double>() + 1) < 1e-8,
            "Focused cell is not central");
      check(Json::parse(d.scene_json()).at("guidePieces").size() == 27, "Guide cap differs");
    }
    // Put the pole at a generic interior point of a curved triangle; clipping
    // must detect interior crossings, not just test the original vertices.
    const double a = .43, c = std::cos(a), s = std::sin(a);
    Json view = {c, 0, 0, -s, 0, 1, 0, 0, 0, 0, 1, 0, s, 0, 0, c};
    auto clipped = g.project_with_options(frame, "stereographic",
                                          Json{{"viewIsometry", view}, {"clipRadius", 2.0}}.dump());
    finite(clipped, 2);
    check(Json::parse(clipped.scene_json()).at("projection").at("clippedTriangles") > 0,
          "Clipping not exercised");
    check(drawing.positions() == saved && session.state_json() == initial,
          "Drawing lifetime or presentation changed state");
    reject([&] { g.project(frame); });
    for (auto options : {Json{{"clipRadius", 0}}, Json{{"focusCell", "bad"}},
                         Json{{"viewIsometry", Json::array()}}, Json{{"bogus", 1}}})
      reject([&] { g.project_with_options(frame, "stereographic", options.dump()); });
    for (int defect = 0; defect < 5; defect++) {
      auto bad = r;
      if (defect == 0)
        bad["assets"]["1/0"]["vertices"][0][0] = 3;
      if (defect == 1)
        bad["domains"]["face"]["placementFrames"]["p0"][0] = .5;
      if (defect == 2)
        bad["operationTracks"]["Xp_fY"]["angle"] = .5;
      if (defect == 3)
        bad["controlLayout"]["groups"][0]["categories"][0]["axes"][0]["direction"] = {1, 0, 0, 0};
      if (defect == 4)
        bad["compatibleDefinitionDigest"] = std::string(64, '0');
      reject([&] { GeometryDefinition invalid(session.execution(), bad.dump()); });
    }
    std::cout
        << "S3: 184 turns, 27-piece caps, SO4 paths, projection, clipping, focus and validation passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
