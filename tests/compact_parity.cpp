#include "twisty/kernel.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
namespace {
std::string read(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("Missing parity input: " + path);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}
void check(bool x, const std::string &message) {
  if (!x)
    throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected baseline fixture");
    auto baseline = Json::parse(read(argv[1]));
    std::size_t checks = 0;
    for (const auto &fixture : baseline) {
      auto name = fixture.at("package").get<std::string>();
      const auto source =
          Json::parse(read(std::string(PROJECT_ROOT) + "/packages/" + name + "/puzzle.pzl.json"));
      auto puzzle = language::compile(source);
      const auto &bindings = puzzle.bindings;
      Index offset = 0;
      for (Index d = 0; d < fixture.at("domains").size(); ++d) {
        const auto &expected = fixture.at("domains")[d];
        check(expected.at("placements") == source.at("domains")[d].at("placements"),
              name + ": changed placement identities");
        check(expected.at("moves").size() == source.at("domains")[d].at("moves").size(),
              name + ": changed movement choices");
        const auto n = static_cast<Index>(expected.at("placements").size());
        for (Index m = 0; m < expected.at("moves").size(); ++m) {
          const auto &move = expected.at("moves")[m];
          const auto &actual = source.at("domains")[d].at("moves")[m];
          check(actual.at("id") == move.at("id") && actual.at("event") == move.at("event"),
                name + ": changed choice/event");
          std::map<Index, Index> rows;
          for (const auto &r : move.at("rows"))
            rows[r[0].get<Index>()] = r[1].get<Index>();
          const auto map = bindings.move_maps[d][m + 1];
          for (Index q = 0; q < n; ++q) {
            const auto to = puzzle.program.transport(map, offset + q);
            const auto wanted = rows.contains(q) ? std::optional<Index>{offset + rows[q]} : std::nullopt;
            check(to == wanted, name + "/" + expected.at("id").get<std::string>() + "/" +
                                    move.at("id").get<std::string>() +
                                    ": transport or participation differs at " + std::to_string(q));
            ++checks;
          }
        }
        offset += n;
      }
      std::cout << name << ": exact transport parity; compact payload "
                << puzzle.program.transport_storage_bytes() << " bytes\n";
      if (fixture.contains("compatibility")) {
        const auto &old = fixture.at("compatibility");
        auto execution = Execution::from_source(old.at("package").dump());
        check(execution->definition()->digest == old.at("digest").get<std::string>(),
              "Version-1 fingerprint changed");
        auto again = Execution::from_source(execution->definition_json().dump());
        check(again->definition()->digest == execution->definition()->digest,
              "Version-1 definition does not round trip");
        Session s(execution);
        check(s.execute("R", "0").at("status") == "Committed", "Version-1 session execution failed");
        Session fresh(execution);
        check(Json::parse(fresh.load_json(s.save_json())).at("status") == "Loaded",
              "Version-1 replay failed");
      }
    }
    std::cout << checks << " exact source/target/participation comparisons passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
