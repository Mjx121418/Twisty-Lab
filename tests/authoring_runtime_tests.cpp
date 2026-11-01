#include "twisty/authoring/runtime.hpp"
#include "twisty/authoring/realization.hpp"
#include "twisty/session.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
std::string read(const std::string &path) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + path);
  if (!file) throw std::runtime_error("Missing authoring fixture.");
  std::ostringstream text; text << file.rdbuf(); return text.str();
}
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    const auto source = read("packages/cube3/package.json");
    Json views = Json::array();
    for (const auto *id : {"cube-euclidean", "cube-spherical"})
      views.push_back(Json::parse(read(std::string("packages/cube3/") + id + ".json")));
    Json reused;
    {
      Session session(source);
      session.execute("R", session.revision());
      const auto before = session.save().dump();
      reused = language::runtime_artifacts(session.execution(), views.dump());
      check(session.save().dump() == before, "Export mutated the authoring session.");
      check(Json::parse(language::runtime_artifacts_from_session_json(session, views.dump())) == reused,
        "WASM entry serialization differs from native export.");
    }
    check(language::runtime_artifacts(source, views.dump()) == reused, "Shared definition export depends on session history.");
    for (const auto *name : {"abstract", "controls", "interpretation"})
      check(reused.at(name) == Json::parse(read(std::string("runtime-puzzles/cube3/") + name + ".json")), "Common contract bytes changed.");
    for (std::size_t i = 0; i < views.size(); ++i) {
      const auto id = views[i].at("kind").get<std::string>();
      const auto prefix = "runtime-puzzles/cube3/realizations/" + id + "/";
      check(reused["views"][i]["payload"] == read(prefix + "geometry.json"), "Payload serialization changed.");
      check(reused["views"][i]["realization"] == Json::parse(read(prefix + "realization.json")), "Envelope changed.");
      const auto single = language::realization_artifacts(source, views[i].dump());
      check(single.at("abstract") == reused.at("abstract") && single.at("interpretation") == reused.at("interpretation") &&
        single.at("realization") == reused["views"][i]["realization"] && single.at("payload") == reused["views"][i]["payload"],
        "Single-view and shared export disagree.");
    }
    Session runtime(Execution::from_artifact(reused.at("abstract").dump()));
    check(Json::parse(language::runtime_artifacts_from_session_json(runtime, views.dump())).at("status") == "Invalid",
      "Authoring export accepted a compiler-free execution.");
    std::cout << "Shared authoring export: exact contracts, ownership, history independence and source-context guard passed.\n";
  } catch (const std::exception &error) { std::cerr << diagnostic_result(error).dump() << '\n'; return 1; }
}
