#include "twisty/kernel.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

using namespace twisty;
namespace {
void check(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::string read(const std::string &file) {
  std::ifstream input(std::string(PROJECT_ROOT) + "/" + file);
  if (!input)
    throw std::runtime_error("Missing " + file);
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}
template <class F> void reject(F action, const std::string &reason) {
  try {
    action();
  } catch (const DiagnosticError &e) {
    check(diagnostic_result(e).at("diagnostics").at(0).at("reasonCode") == reason,
          "Wrong diagnostic: " + std::string(e.what()));
    return;
  }
  throw std::runtime_error("Expected rejection: " + reason);
}
void rigid(const GeometryFrame &frame) {
  const auto &m = frame.isometries();
  for (std::size_t i = 0; i < m.size(); i += 16) {
    for (int a = 0; a < 3; ++a)
      for (int b = 0; b < 3; ++b) {
        double dot = 0;
        for (int r = 0; r < 3; ++r)
          dot += m[i + a * 4 + r] * m[i + b * 4 + r];
        check(std::abs(dot - (a == b ? 1 : 0)) < 1e-8, "Mesh scale leaked into an intrinsic pose");
      }
    check(m[i + 3] == 0 && m[i + 7] == 0 && m[i + 11] == 0 && m[i + 15] == 1, "Invalid current-space pose");
  }
}
void realization(Session &session, const std::string &path, const std::string &record) {
  auto definition = std::make_shared<GeometryDefinition>(session.execution(), read(path));
  const GeometryDefinition shared = *definition;
  auto animation = definition->animate(record);
  const auto transition = Json::parse(record);
  const auto snapshot = session.snapshot();
  const auto source = definition->frame(transition.at("beforeState").dump());
  const auto target = definition->frame(transition.at("afterState").dump());
  check(animation.evaluate(0).isometries() == source.isometries(), "Source pose differs");
  check(animation.evaluate(1).isometries() == target.isometries(), "Target pose differs");
  check(animation.evaluate(-4).isometries() == source.isometries(), "Lower clamp differs");
  check(animation.evaluate(4).isometries() == target.isometries(), "Upper clamp differs");
  const auto quarter = animation.evaluate(.25);
  animation.evaluate(.875);
  check(quarter.isometries() == animation.evaluate(.25).isometries(), "Seeking accumulates motion");
  rigid(source);
  rigid(target);
  rigid(quarter);
  GeometryInstance compatibility(definition);
  check(Json::parse(compatibility.prepare_animation_json(record)).at("status") == "Prepared",
        "Compatibility preparation failed");
  compatibility.sample(.25);
  check(compatibility.transforms() == definition->project(animation.evaluate(.15625)).transforms(),
        "Compatibility easing changed");
  const auto part = Json::parse(definition->scene_json()).at("visualParts").at(0);
  const auto hit = Json::parse(definition->resolve_part_json(part.at("visualPartId")));
  check(hit.at("pieceId") == part.at("pieceId") && !hit.contains("operationCandidates"),
        "Part lookup selects requests");
  check(Json::parse(definition->resolve_part_json("missing")).at("status") == "NoTarget",
        "Unknown part resolved");
  auto broken = transition;
  broken.at("request").erase("choices");
  reject([&] { definition->animate(broken.dump()); }, "realization.witness");
  reject([&] { animation.evaluate(std::numeric_limits<double>::infinity()); }, "animation.progress");
  reject([&] { definition->project(source, "poincare-ball"); }, "geometry.projection");
  GeometryDefinition independent(session.execution(), read(path));
  reject([&] { independent.project(source); }, "geometry.frame_mismatch");
  check(session.snapshot() == snapshot, "Presentation mutated the abstract session");
  const auto expected = definition->project(quarter).transforms();
  auto second = shared.animate(record);
  second.evaluate(.5);
  definition.reset();
  check(shared.project(animation.evaluate(.25)).transforms() == expected,
        "Shared definition or independent animation lost data");
  check(shared.positions().data() == compatibility.positions().data(), "Instances duplicated mesh assets");
}
void lifetime() {
  std::optional<GeometryAnimation> animation;
  std::optional<GeometryFrame> frame;
  std::optional<GeometryDrawing> drawing;
  std::vector<double> expected;
  {
    Session session(read("packages/cube3/package.json"));
    GeometryDefinition definition(session.execution(), read("packages/cube3/cube-euclidean.json"));
    const auto result = session.execute("R", session.revision());
    animation = definition.animate(result.at("transitions").at(0).dump());
    frame = animation->evaluate(.25);
    expected = frame->isometries();
    drawing = definition.project(*frame);
  }
  check(animation->evaluate(.25).isometries() == expected && frame->isometries() == expected,
        "Animation/frame did not own shared storage");
  check(!drawing->positions().empty() && !drawing->transforms().empty(), "Drawing lost its assets");
}
void diagram_timing() {
  Session session(read("packages/helicopter/package.json"));
  const auto result = session.execute("UF_ab", session.revision());
  const auto record = result.at("transitions").at(0).dump();
  GeometryDefinition diagram(session.execution(), read("packages/helicopter/helicopter-port-diagram.json"));
  GeometryDefinition physical(session.execution(), read("packages/helicopter/helicopter-euclidean.json"));
  const auto hint = diagram.animate(record).duration_hint();
  check(hint > 0 && hint < 220 && hint == physical.animate(record).duration_hint(),
        "A short uniform turn acquired a minimum diagram duration");
}
} // namespace
int main() {
  try {
    const auto inventory = Json::parse(read("scripts/puzzle-inventory.json"));
    std::size_t views = 0;
    for (const auto &puzzle : inventory.at("puzzles")) {
      const auto dir = "packages/" + puzzle.at("id").get<std::string>() + "/";
      if (puzzle.at("id") == "tesseract3") continue; // Non-affine projection has separate S3 coverage.
      Session session(read(dir + "package.json"));
      const auto operation = session.snapshot().at("legalRequests").at(0).at("operation").get<std::string>();
      const auto result = session.execute(operation, session.revision());
      check(result.at("status") == "Committed", "Initial move failed");
      const auto record = result.at("transitions").at(0).dump();
      for (const auto &file : puzzle.at("realizations")) {
        realization(session, dir + file.get<std::string>(), record);
        ++views;
      }
    }
    lifetime();
    diagram_timing();
    std::cout
        << views
        << " realizations: explicit poses, projection, seeking, ownership and witness validation passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
