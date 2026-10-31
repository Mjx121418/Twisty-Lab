#include <twisty/geometry.hpp>
#include <twisty/runtime_api.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

std::string read(const char *path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() < 0 || file.tellg() > 64 * 1024 * 1024)
    throw std::runtime_error("Artifact input is missing or exceeds 64 MiB.");
  file.seekg(0); std::ostringstream text; text << file.rdbuf(); return text.str();
}
int main(int argc, char **argv) {
  try {
    if (argc != 5 && argc != 6) throw std::runtime_error("Expected abstract artifact, interpretation, realization envelope, payload and optional controls paths.");
    const auto execution = argc == 6 ? twisty::runtime_execution(read(argv[1]), read(argv[5]))
      : twisty::Execution::from_artifact(read(argv[1]));
    twisty::GeometryDefinition geometry(execution, read(argv[2]), read(argv[3]), read(argv[4]));
    auto frame = geometry.frame(twisty::encode_state(*execution->definition(), execution->definition()->initial).dump());
    const auto scene = twisty::Json::parse(geometry.scene_json());
    std::cout << twisty::Json{{"status", "Valid"}, {"executionDigest", execution->definition()->digest},
      {"interpretationDigest", scene.at("interpretationDigest")}, {"realizationDigest", scene.at("realizationDigest")},
      {"space", twisty::Json::parse(geometry.space_json())}, {"parts", frame.isometries().size() / 16}}.dump() << '\n';
  } catch (const std::exception &error) { std::cerr << twisty::diagnostic_result(error).dump() << '\n'; return 1; }
}
