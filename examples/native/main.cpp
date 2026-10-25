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
      throw std::runtime_error("Expected definition, realization, algorithm/intent, and optional piece-ID array.");
    twisty::Session session(read(argv[1]));
    const auto selection = argc == 5 ? twisty::Controller(session.execution()).families_for_pieces(
        session, twisty::Json::parse(argv[4]).get<std::vector<std::string>>()) : twisty::Json();
    twisty::GeometryDefinition geometry(session.execution(), read(argv[2]));
    auto drawing = geometry.project(geometry.frame(session.state_json()));
    const auto result = argv[3][0] == '{'
                            ? twisty::Controller(session.execution())
                                  .turn_to(session, twisty::Json::parse(argv[3]), session.revision())
                            : session.run(argv[3], "transactional", session.revision());
    if (result.at("status") != "Committed")
      throw std::runtime_error(result.dump());
    for (const auto &transition : result.at("transitions")) {
      const auto animation = geometry.animate(transition.dump());
      // Progress is explicit and linear. A frontend may apply its own easing.
      const auto middle = geometry.project(animation.evaluate(0.5));
      (void)middle;
      drawing = geometry.project(animation.evaluate(1));
    }
    auto output = twisty::Json{{"kernel", twisty::kernel_info()},
                              {"snapshot", session.snapshot()},
                              {"scene", twisty::Json::parse(geometry.scene_json())},
                              {"transforms", drawing.transforms()}};
    if (argc == 5) output["familySelection"] = selection;
    std::cout << output.dump() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
