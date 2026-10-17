#include "twisty/kernel.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::string read(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file)
    throw std::runtime_error("Missing runtime fixture: " + path.string());
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}
void puzzle(const std::string &name) {
  const auto directory = std::filesystem::path(PROJECT_ROOT) / "packages" / name;
  Session session(read(directory / "relational-package.json"));
  Session restored(session.execution());
  const auto before = session.snapshot();
  const auto request = before.at("legalRequests").at(0).at("operation").get<std::string>();
  const auto result = session.execute(request, session.revision());
  check(result.at("status") == "Committed", "Runtime move failed");
  const auto &transition = result.at("transitions").at(0);
  check(restored.load(session.save()).at("status") == "Loaded", "Runtime replay failed");
  check(restored.state() == session.state(), "Shared-definition replay differs");
  check(session.undo(session.revision()).at("status") == "Committed" &&
            session.snapshot().at("stateDigest") == before.at("stateDigest"),
        "Runtime inverse failed");
  check(session.redo(session.revision()).at("status") == "Committed", "Runtime redo failed");
  std::size_t views = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    const auto file = entry.path().filename().string();
    if (!file.starts_with("relational-") || !file.ends_with(".json"))
      continue;
    const auto realization = Json::parse(read(entry.path()));
    if (!realization.contains("compatibleDefinitionDigest"))
      continue;
    Geometry view(session.execution(), realization.dump());
    const auto scene = Json::parse(view.scene_json());
    check(!scene.at("visualParts").empty(), "Runtime view is empty");
    check(view.positions().size() == view.normals().size(), "Normal buffer differs");
    check(Json::parse(view.prepare_animation_json(transition.dump())).at("status") == "Prepared",
          "Runtime rejected its completed witness");
    view.sample(.5);
    for (const auto x : view.transforms())
      check(std::isfinite(x), "Runtime animation is nonfinite");
    view.sample(1);
    const auto endpoint = view.transforms();
    view.set_state_json(session.state_json());
    check(view.transforms() == endpoint, "Runtime animation endpoint differs from its exact state");
    auto corrupt = transition;
    corrupt["request"].erase("choices");
    check(Json::parse(view.prepare_animation_json(corrupt.dump())).at("status") == "UnsupportedTransition" &&
              view.transforms() == endpoint,
          "Runtime accepted an incomplete witness or changed a rejected frame");
    ++views;
  }
  check(views >= 3, "Runtime puzzle is missing a view");
  std::cout << name << ": session, replay, inverse and " << views << " runtime views passed\n";
}
} // namespace
int main() {
  try {
    for (const auto *name : {"cube3", "bandaged", "helicopter", "bagua", "gear", "gear-extreme", "gear-plain"})
      puzzle(name);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
