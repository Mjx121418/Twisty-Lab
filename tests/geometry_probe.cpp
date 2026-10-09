#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace twisty;
namespace {
std::string read(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Cannot read " + path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::runtime_error("Expected definition, realization, and algorithm.");
    Session session(read(argv[1]));
    Geometry geometry(session.definition_json(), read(argv[2]));
    Json report{{"scene", Json::parse(geometry.scene_json())},
                {"positions", geometry.positions()},
                {"normals", geometry.normals()},
                {"indices", geometry.indices()},
                {"initial", geometry.transforms()},
                {"frames", Json::array()}};
    const auto result = session.run(argv[3], "transactional", "0");
    if (result.at("status") != "Committed")
      throw std::runtime_error(result.dump());
    for (const auto &transition : result.at("transitions")) {
      const auto prepared = Json::parse(geometry.prepare_animation_json(transition.dump()));
      if (prepared.at("status") != "Prepared")
        throw std::runtime_error(prepared.dump());
      Json frame{{"operation", transition.at("request").at("operation")}, {"source", geometry.transforms()}};
      geometry.sample(0.5);
      frame["middle"] = geometry.transforms();
      geometry.sample(1);
      frame["target"] = geometry.transforms();
      report["frames"].push_back(frame);
    }
    report["stateDigest"] = session.snapshot().at("stateDigest");
    std::cout << report.dump() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
