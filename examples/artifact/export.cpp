#include <twisty/authoring/realization.hpp>
#include <twisty/geometry.hpp>
#include <twisty/authoring/runtime.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

std::string read(const char *path) {
  std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot read authoring input.");
  std::ostringstream text; text << file.rdbuf(); return text.str();
}
int main(int argc, char **argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--runtime-bundle") {
      std::cout << twisty::language::runtime_artifacts(read(argv[2]), read(argv[3])).dump() << '\n';
      return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--validate-source") {
      auto execution = twisty::Execution::from_source(read(argv[2]));
      execution->validate(execution->definition()->initial);
      twisty::GeometryDefinition geometry(execution, read(argv[3]));
      auto frame = geometry.frame(twisty::encode_state(*execution->definition(), execution->definition()->initial).dump());
      std::cout << twisty::Json{{"status", "Valid"}, {"definitionDigest", execution->definition()->digest},
        {"space", twisty::Json::parse(geometry.space_json())}, {"parts", frame.isometries().size() / 16}}.dump() << '\n';
      return 0;
    }
    if (argc != 3) throw std::runtime_error("Expected source package and realization paths.");
    std::cout << twisty::language::realization_artifacts(read(argv[1]), read(argv[2])).dump() << '\n';
  } catch (const std::exception &error) { std::cerr << twisty::diagnostic_result(error).dump() << '\n'; return 1; }
}
