#include <fstream>
#include <iostream>
#include <sstream>
#include <twisty/authoring/compiler.hpp>
#include <twisty/authoring/cube.hpp>
#include <twisty/authoring/gear.hpp>

// A standalone consumer, also used by the independent source-semantics oracle.
int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 3)
      throw std::runtime_error(
          "Usage: twisty_language source.pzl.json [--inspect] | --cube-module | --bandaged-module");
    if (argc == 2 &&
        (std::string(argv[1]) == "--cube-module" || std::string(argv[1]) == "--bandaged-module")) {
      std::cout << twisty::language::cube_module_json(std::string(argv[1]) == "--bandaged-module") << '\n';
      return 0;
    }
    std::ifstream file(argv[1]);
    if (!file)
      throw std::runtime_error("Cannot open puzzle source");
    std::ostringstream source;
    source << file.rdbuf();
    if (argc == 3 && std::string(argv[2]) == "--gear-module") {
      std::cout << twisty::language::gear_module_json(source.str()) << '\n';
      return 0;
    }
    twisty::language::Puzzle puzzle(source.str());
    if (argc == 3) {
      if (std::string(argv[2]) != "--inspect")
        throw std::runtime_error("Expected --inspect");
      std::cout << puzzle.inspect_json() << '\n';
    } else {
      std::string line;
      while (std::getline(std::cin, line))
        std::cout << puzzle.query_json(line) << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
