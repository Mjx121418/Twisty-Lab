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
    std::size_t checks = 0, extended_rows = 0, retired_pairs = 0;
    for (const auto &fixture : baseline) {
      auto name = fixture.at("package").get<std::string>();
      const auto source =
          Json::parse(read(std::string(PROJECT_ROOT) + "/packages/" + name + "/puzzle.pzl.json"));
      auto puzzle = language::compile(source);
      const auto &bindings = puzzle.bindings;
      const bool melty = name.starts_with("melty-");
      Index offset = 0;
      for (Index d = 0; d < fixture.at("domains").size(); ++d) {
        const auto &expected = fixture.at("domains")[d];
        const auto &current = source.at("domains")[d].at("placements");
        const bool lifted = name == "helicopter" && expected.at("id") == "mechanism";
        check(melty ? current.size() >= expected.at("placements").size() &&
                   std::equal(expected.at("placements").begin(), expected.at("placements").end(), current.begin()) :
              lifted ? current.size() == 2 * expected.at("placements").size() : expected.at("placements") == current,
              name + ": changed placement identities outside declared phase refinement");
        std::map<std::string,Index> old_ids;
        for(Index q=0;q<expected.at("placements").size();++q)
          old_ids.emplace(expected.at("placements")[q].get<std::string>(),q);
        const auto project = [&](Index q) {
          auto key=current.at(q).get<std::string>();
          if(lifted && key.back()>='d' && key.back()<='f') key.back()-=3;
          check(old_ids.contains(key),"Phase projection lost an old placement");
          return old_ids.at(key);
        };
        check(melty || expected.at("moves").size() == source.at("domains")[d].at("moves").size(),
              name + ": changed movement choices");
        const auto n = static_cast<Index>(current.size());
        for (Index m = 0; m < expected.at("moves").size(); ++m) {
          const auto &move = expected.at("moves")[m];
          if (melty && move.at("id").get<std::string>().find('+') != std::string::npos) {
            ++retired_pairs;
            continue;
          }
          const auto &moves = source.at("domains")[d].at("moves");
          auto found = std::find_if(moves.begin(), moves.end(), [&](const auto &x) {return x.at("id") == move.at("id");});
          check(found != moves.end(), name + ": lost a single-cap choice");
          const auto index = static_cast<Index>(found - moves.begin());
          const auto &actual = *found;
          check(actual.at("id") == move.at("id") && actual.at("event") == move.at("event"),
                name + ": changed choice/event");
          std::map<Index, Index> rows;
          for (const auto &r : move.at("rows"))
            rows[r[0].get<Index>()] = r[1].get<Index>();
          const auto map = bindings.move_maps[d][index + 1];
          for (Index q = 0; q < (melty ? expected.at("placements").size() : n); ++q) {
            const auto to = puzzle.program.transport(map, offset + q);
            if (melty && !rows.contains(q)) {
              if (to) ++extended_rows;
              continue;
            }
            const auto old_q=project(q);
            const auto wanted = rows.contains(old_q) ? std::optional<Index>{rows[old_q]} : std::nullopt;
            const auto projected = to ? std::optional<Index>{project(*to-offset)} : std::nullopt;
            check(projected == wanted, name + "/" + expected.at("id").get<std::string>() + "/" +
                                    move.at("id").get<std::string>() +
                                    ": transport or participation differs at " + std::to_string(q));
            ++checks;
          }
        }
        offset += n;
      }
      std::cout << name << (melty ? ": retained single-cap rows agree; expanded catalog payload " : name == "helicopter" ? ": phase-projected transport parity; compact payload " : ": exact transport parity; compact payload ")
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
    std::cout << checks << " source/target/participation comparisons passed (Helicopter uses phase projection)\n"
              << extended_rows << " additional Melty rows; " << retired_pairs << " retired paired choices\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
