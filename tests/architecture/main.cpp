// Installed native consumer: keep this independent of private compiler headers.
#include <twisty/kernel.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace twisty;
namespace {
using Clock = std::chrono::steady_clock;
std::string read(const std::string &path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read " + path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
Json samples(const std::vector<double> &values) {
  auto result = Json::array();
  const auto stride = std::max(std::size_t{1}, values.size() / 32);
  for (std::size_t i = 0; i < values.size(); i += stride)
    result.push_back(values[i]);
  return result;
}
Json peak_rss() {
#if defined(__linux__) || defined(__APPLE__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__linux__)
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#else
    return usage.ru_maxrss;
#endif
  }
#endif
  return nullptr;
}
} // namespace
int main(int argc, char **argv) {
  try {
    require(argc == 3, "Expected workspace root and scenario JSON.");
    const std::string root = argv[1];
    const auto scenario = Json::parse(argv[2]);
    const auto source = read(root + "/" + scenario.at("package").get<std::string>());
    const auto start = Clock::now();
    Session session(source);
    session.lightweight_results(true);
    Json metrics{{"sessionLoadMs", elapsed(start)}, {"packageBytes", source.size()}};
    const auto definition = session.definition_json();
    Json observed{{"kernel", kernel_info()}, {"definitionSHA256", sha256(definition)},
                  {"definitionBytes", definition.size()},
                  {"initial", session.snapshot(SnapshotDetail::StateOnly)}};
    const auto first = scenario.at("firstOperation").get<std::string>();
    std::optional<Controller> controller;
    if (scenario.contains("controller")) {
      const auto &config = scenario.at("controller");
      controller.emplace(session.execution());
      const auto plan = controller->plan_to(session, config.at("intent"), session.revision());
      require(plan.at("status") == "Planned" && session.revision() == "0", "Controller preview failed or mutated state.");
      require(controller->turn_to(session, config.at("intent"), "999").at("status") == "StaleRevision",
              "Controller accepted a stale intent.");
      observed["controller"] = {{"initial", controller->family_state(session, config.at("family"))},
        {"selection", controller->families_for_pieces(session, config.at("pieces").get<std::vector<std::string>>())},
        {"request", plan.at("request")}, {"planSHA256", sha256(plan.dump())}};
    }
    require(session.execute(first, "999").at("status") == "StaleRevision" && session.revision() == "0",
            "Stale request changed session.");
    if (scenario.contains("blockedOperation")) {
      const auto before = session.state_json();
      const auto blocked = session.execute(scenario.at("blockedOperation"), session.revision());
      require(blocked.at("status") == "Blocked" && before == session.state_json(),
              "Expected blocked request to preserve state.");
      observed["blockedReason"] = blocked.at("reasonCode");
    }
    const auto result = session.run(scenario.at("algorithm"), "transactional", session.revision());
    require(result.at("status") == "Committed" && !result.at("transitions").empty(), "Algorithm failed.");
    observed["after"] = session.snapshot(SnapshotDetail::StateOnly);
    if (controller) {
      const auto &config = scenario.at("controller");
      observed["controller"]["after"] = controller->family_state(session, config.at("family"));
      observed["controller"]["selectionAfter"] = controller->families_for_pieces(session, config.at("pieces").get<std::vector<std::string>>());
    }
    observed["witnesses"] = Json::array();
    for (const auto &transition : result.at("transitions")) {
      std::size_t loops = 0;
      for (const auto &action : transition.at("pieceActions"))
        if (action.at("from") == action.at("to")) ++loops;
      observed["witnesses"].push_back({{"request", transition.at("request")},
        {"sha256", sha256(transition.dump())}, {"participants", transition.at("pieceActions").size()},
        {"selfLoops", loops}});
    }
    observed["saveSHA256"] = sha256(session.save_json());
    const auto target = session.state_json();
    require(session.undo(session.revision()).at("status") == "Committed", "Undo failed.");
    const auto save = session.save_json();
    observed["redoTailSaveSHA256"] = sha256(save);
    Session restored(session.execution());
    restored.lightweight_results(true);
    require(Json::parse(restored.load_json(save)).at("status") == "Loaded", "Replay failed.");
    require(restored.redo(restored.revision()).at("status") == "Committed" && restored.state_json() == target,
            "Replay lost recorded redo choices.");
    require(session.redo(session.revision()).at("status") == "Committed" && session.state_json() == target,
            "Redo differs from original result.");
    observed["views"] = Json::array();
    metrics["geometry"] = Json::array();
    for (const auto &file : scenario.at("realizations")) {
      const auto text = read(root + "/" + file.get<std::string>());
      const auto geometry_start = Clock::now();
      GeometryDefinition geometry(session.execution(), text);
      metrics["geometry"].push_back({{"compileMs", elapsed(geometry_start)}, {"realizationBytes", text.size()}});
      const auto scene = Json::parse(geometry.scene_json());
      Json view{{"id", scene.at("realizationId")}, {"space", Json::parse(geometry.space_json())},
        {"parts", scene.at("visualParts").size()}, {"positions", geometry.positions().size()},
        {"indices", geometry.indices().size()}, {"frames", Json::array()}};
      for (const auto &transition : result.at("transitions")) {
        const auto animation = geometry.animate(transition.dump());
        const auto a = animation.evaluate(0), b = animation.evaluate(1), middle = animation.evaluate(.5);
        require(std::all_of(middle.isometries().begin(), middle.isometries().end(),
                           [](double value) { return std::isfinite(value); }), "Nonfinite intermediate pose.");
        require(a.isometries() == geometry.frame(transition.at("beforeState").dump()).isometries(), "Source differs.");
        require(b.isometries() == geometry.frame(transition.at("afterState").dump()).isometries(), "Endpoint differs.");
        animation.evaluate(.875);
        require(middle.isometries() == animation.evaluate(.5).isometries(), "Seeking accumulated motion.");
        view["frames"].push_back({{"durationHint", animation.duration_hint()},
          {"source", samples(a.isometries())}, {"middle", samples(middle.isometries())}, {"target", samples(b.isometries())}});
      }
      auto forged = result.at("transitions").at(0);
      forged["request"].erase("choices");
      bool rejected = false;
      try { geometry.animate(forged.dump()); }
      catch (const DiagnosticError &error) { rejected = error.code == "realization.witness"; }
      require(rejected, "Incomplete witness was accepted.");
      observed["views"].push_back(std::move(view));
    }
    metrics["peakRssBytes"] = peak_rss();
    std::cout << Json{{"observed", observed}, {"metrics", metrics}}.dump() << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
