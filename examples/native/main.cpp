#include <fstream>
#include <iostream>
#include <sstream>
#include <twisty/kernel.hpp>

std::string read(const char *path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Cannot read input file.");
  std::ostringstream result;
  result << input.rdbuf();
  return result.str();
}
int main(int argc, char **argv) {
  try {
    if (argc != 4 && argc != 5)
      throw std::runtime_error(
          "Expected definition, realization, algorithm/intent, and optional piece-ID array.");
    twisty::Session session(read(argv[1]));
    const auto selection =
        argc == 5
            ? twisty::Controller(session.execution())
                  .families_for_pieces(session, twisty::Json::parse(argv[4]).get<std::vector<std::string>>())
            : twisty::Json();
    twisty::GeometryDefinition geometry(session.execution(), read(argv[2]));
    const auto scene = twisty::Json::parse(geometry.scene_json());
    const auto projection = scene.at("projections").at(0).get<std::string>();
    auto drawing = geometry.project(geometry.frame(session.state_json()), projection);
    twisty::Json projectedSamples = twisty::Json::array();
    const auto result = argv[3][0] == '{'
                            ? twisty::Controller(session.execution())
                                  .turn_to(session, twisty::Json::parse(argv[3]), session.revision())
                            : session.run(argv[3], "transactional", session.revision());
    if (result.at("status") != "Committed")
      throw std::runtime_error(result.dump());
    for (const auto &transition : result.at("transitions")) {
      const auto animation = geometry.animate(transition.dump());
      // Progress is explicit and linear. A frontend may apply its own easing.
      const auto middle = geometry.project(animation.evaluate(0.5), projection);
      if (scene.value("dynamicMeshes", false)) {
        twisty::Json sample = {{"positionCount", middle.positions().size()},
                               {"indexCount", middle.indices().size()},
                               {"positions", twisty::Json::array()},
                               {"normals", twisty::Json::array()}};
        for (std::size_t i = 0; i < middle.positions().size();
             i += std::max(std::size_t{1}, middle.positions().size() / 64)) {
          sample["positions"].push_back(middle.positions()[i]);
          sample["normals"].push_back(middle.normals()[i]);
        }
        projectedSamples.push_back(sample);
      }
      drawing = geometry.project(animation.evaluate(1), projection);
    }
    auto output = twisty::Json{{"kernel", twisty::kernel_info()},
                               {"snapshot", session.snapshot()},
                               {"scene", twisty::Json::parse(drawing.scene_json())},
                               {"transforms", drawing.transforms()}};
    if (scene.value("dynamicMeshes", false))
      output["projectedSamples"] = projectedSamples;
    if (scene.at("space").at("id") == "S3") {
      // An independent GPU renderer can retain this snapshot, upload its 4D
      // prototypes once, and update only frame.isometries() during animation.
      const auto intrinsic = geometry.intrinsic_mesh();
      output["intrinsicMesh"] = {{"descriptor", twisty::Json::parse(intrinsic.descriptor_json())},
                                  {"positions", intrinsic.positions()}, {"normals", intrinsic.normals()},
                                  {"indices", intrinsic.indices()}};
    }
    if (argc == 5)
      output["familySelection"] = selection;
    std::cout << output.dump() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
