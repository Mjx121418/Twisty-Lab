#include "twisty/kernel.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace twisty;
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected a package path");
    std::ifstream f(argv[1]);
    std::ostringstream in;
    in << f.rdbuf();
    const auto text = in.str();
    auto raw = Json::parse(text);
    auto program = language::compile(raw.at("source"));
    const auto bytes = program.program.transport_storage_bytes();
    const auto start = std::chrono::steady_clock::now();
    auto e = Execution::from_source(text);
    const auto end = std::chrono::steady_clock::now();
    const auto initial = e->definition()->initial;
    const auto op = e->legal(initial).at(0).at("operation").get<std::string>();
    const auto loop_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
      auto forward = e->plan(initial, op);
      if (!forward.transition)
        throw std::runtime_error("Benchmark move rejected");
      auto back = e->inverse(*forward.transition);
      if (!back.transition || back.transition->after != initial)
        throw std::runtime_error("Benchmark inverse failed");
    }
    const auto loop_end = std::chrono::steady_clock::now();
    std::cout << Json{{"loadMilliseconds", std::chrono::duration<double, std::milli>(end - start).count()},
                      {"planMicroseconds",
                       std::chrono::duration<double, std::micro>(loop_end - loop_start).count() / 200},
                      {"payloadBytes", bytes}}
                     .dump()
              << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
