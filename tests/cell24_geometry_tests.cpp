#include "twisty/kernel.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace twisty;
namespace {
void check(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
std::string read(const std::string &path) {
  std::ifstream f(std::string(PROJECT_ROOT) + "/" + path);
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
  throw std::runtime_error("Expected invalid geometry rejection");
}
void finite(const GeometryDrawing &drawing) {
  check(!drawing.positions().empty(), "Missing drawing");
  for (const auto x : drawing.positions())
    check(std::isfinite(x), "Nonfinite projection");
  for (const auto x : drawing.normals())
    check(std::isfinite(x), "Nonfinite normal");
}
} // namespace
int main() {
  try {
    Session session(read("packages/cell24/package.json"));
    session.lightweight_results(true);
    const auto initial = session.state_json();
    const auto r = Json::parse(read("packages/cell24/cell24-s3.json"));
    GeometryDefinition geometry(session.execution(), r.dump());
    const auto intrinsic = geometry.intrinsic_mesh();
    const auto descriptor = Json::parse(intrinsic.descriptor_json());
    check(descriptor.at("visualParts").size() == 1368 && descriptor.at("meshAssets").size() == 15,
          "Unshared or missing colored meshes");
    for (const auto &part : descriptor.at("visualParts"))
      check(part.at("cutHalfspaces").size() == 24, "Missing complete cut membership");
    const auto &definition = *session.execution()->definition();
    check(definition.pieces.size() == 672 && definition.operations.size() == 552, "Wrong 24-cell inventory");
    for (const auto &op : definition.operations) {
      const auto result = session.execute(op.id, session.revision());
      check(result.at("status") == "Committed", op.id + " unavailable");
      const auto &transition = result.at("transitions")[0];
      check(transition.at("pieceActions").size() == 105, "Incomplete 105-piece cap");
      auto animation = geometry.animate(transition.dump());
      const auto middle = animation.evaluate(.37);
      for (const auto x : middle.isometries())
        check(std::isfinite(x), "Nonfinite animation");
      check(animation.evaluate(1).isometries() ==
                geometry.frame(transition.at("afterState").dump()).isometries(),
            "Animation endpoint mismatch");
      session.execute(op.inverse, session.revision());
      check(session.state_json() == initial, "Inverse did not restore solved state");
    }
    check(session.run("Xp_fXYZ Hpppp_eXY Yp_vX Hpmmp_vZ Wm_fXYmZ", "transactional", session.revision())
                  .at("status") == "Committed",
          "Mixed sequence failed");
    const auto scrambled = session.state_json();
    const auto frame = geometry.frame(scrambled);
    for (const auto &group : r.at("controlLayout").at("groups")) {
      const auto id = group.at("id").get<std::string>();
      const auto operation = group.at("categories")[0]["axes"][0]["turns"][0]["operation"].get<std::string>();
      const auto result = session.execute(operation, session.revision());
      check(result.at("status") == "Committed" && result.at("transitions")[0]["pieceActions"].size() == 105,
            "Scrambled cap unavailable");
      session.execute(definition.operations[definition.operation_ids.at(operation)].inverse,
                      session.revision());
      check(session.state_json() == scrambled, "Scrambled inverse mismatch");
      const auto drawing = geometry.project_with_options(
          frame, "stereographic",
          Json{{"focusCell", id},
               {"guide", {{"group", id}, {"axis", group.at("categories")[0]["axes"][0]["id"]}}}}
              .dump());
      finite(drawing);
      check(Json::parse(drawing.scene_json()).at("guidePieces").size() == 105, "Incorrect guide membership");
    }
    auto invalid = r;
    invalid["requiredCapabilities"].erase(invalid["requiredCapabilities"].end()-1);
    reject([&] { GeometryDefinition g(session.execution(), invalid.dump()); });
    invalid = r;
    invalid["domains"]["s"]["cutMembership"][0] = false;
    reject([&] { GeometryDefinition g(session.execution(), invalid.dump()); });
    invalid = r;
    invalid["operationTracks"]["Xp_fXYZ"]["angle"] = .5;
    reject([&] { GeometryDefinition g(session.execution(), invalid.dump()); });
    invalid = r;
    invalid["cutDepth"] = .5;
    reject([&] { GeometryDefinition g(session.execution(), invalid.dump()); });
    invalid = r;
    invalid["domains"]["s"]["placementFrames"]["p0"][0] = .3;
    reject([&] { GeometryDefinition g(session.execution(), invalid.dump()); });
    std::cout << "24-cell native: all 552 turns, endpoints/inverses, scrambled caps, 24 focus charts, and "
                 "invalid geometry passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
