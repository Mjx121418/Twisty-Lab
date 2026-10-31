#include <twisty/authoring/realization.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

std::string read(const char *path) {
  std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot read authoring input.");
  std::ostringstream text; text << file.rdbuf(); return text.str();
}
int main(int argc, char **argv) {
  try {
    if (argc != 3) throw std::runtime_error("Expected source package and realization paths.");
    std::cout << twisty::language::realization_artifacts(read(argv[1]), read(argv[2])).dump() << '\n';
  } catch (const std::exception &error) { std::cerr << twisty::diagnostic_result(error).dump() << '\n'; return 1; }
}
