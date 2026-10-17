#include <fstream>
#include <iostream>
#include <sstream>
#include <twisty/relational_compiler.hpp>

// A standalone consumer, also used by the independent source-semantics oracle.
int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 3)
      throw std::runtime_error("Usage: twisty_relational source.pzl.json [--inspect]");
    std::ifstream file(argv[1]);
    if (!file)
      throw std::runtime_error("Cannot open puzzle source");
    std::ostringstream source;
    source << file.rdbuf();
    twisty::relational::Puzzle puzzle(source.str());
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
