// Internal storage inspection. Public controller timing uses benchmark-controller.mjs.
#include "../src/interpreter/geometry_compiler.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace twisty;
namespace {
std::string read(const char *path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("Cannot read benchmark input.");
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("Expected package and realization paths.");
    auto execution = Execution::from_source(read(argv[1]));
    auto program = GeometryCompiler(execution, read(argv[2])).compile();
    std::size_t bytes = 0, indices = 0, regions = 0, steps = 0;
    std::set<const void *> libraries;
    for (const auto &table : program->tables) {
      bytes += table.tracks.capacity() * sizeof(GeometryTrack);
      for (const auto &track : table.tracks) {
        indices += track.placement_paths.size();
        regions += track.regions.size();
        steps += track.uniform.size();
        bytes += track.placement_paths.capacity() * sizeof(Index) +
                 track.regions.capacity() * sizeof(GeometryTrack::Region) +
                 track.uniform.capacity() * sizeof(GeometryRotation);
        if (track.path_library && libraries.insert(track.path_library.get()).second) {
          bytes += track.path_library->capacity() * sizeof(std::vector<GeometryRotation>);
          for (const auto &path : *track.path_library) {
            steps += path.size();
            bytes += path.capacity() * sizeof(GeometryRotation);
          }
        }
      }
    }
    if (execution->definition()->operations.empty())
      throw std::runtime_error("Storage benchmark requires an operation.");
    const auto payload =
        execution->definition()->operations[0].maps[0].view->program->transport_storage_bytes();
    std::cout << Json{{"transportPayloadBytes", payload},
                      {"pathBackingBytes", bytes},
                      {"densePathIndices", indices},
                      {"regions", regions},
                      {"rotationSteps", steps}}
                     .dump()
              << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
