#include <twisty/artifact.hpp>
#include <twisty/session.hpp>
#include <twisty/geometry.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
std::string read(const char *path) {
  std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot read artifact input.");
  std::ostringstream text; text << file.rdbuf(); return text.str();
}
int main(int argc, char **argv) {
  try {
    if (argc != 5 && argc != 6) throw std::runtime_error("Expected abstract artifact, interpretation, realization, optional payload, and algorithm.");
    const auto abstract = read(argv[1]);
    Session session(Execution::from_artifact(abstract)); session.lightweight_results(true);
    const auto initial = session.snapshot(SnapshotDetail::StateOnly);
    auto geometry = argc == 6 ? GeometryDefinition(session.execution(), read(argv[2]), read(argv[3]), read(argv[4]))
                             : GeometryDefinition(session.execution(), read(argv[2]), read(argv[3]));
    const auto scene = Json::parse(geometry.scene_json());
    const auto result = session.run(argv[argc - 1], "transactional", session.revision());
    if (result.at("status") != "Committed") throw std::runtime_error(result.dump());
    Json frames = Json::array();
    for (const auto &record : result.at("transitions")) {
      const auto animation = geometry.animate(record.dump());
      Json sample = Json::object();
      for (double progress : {0.0, 0.5, 1.0}) {
        const auto pose = animation.evaluate(progress);
        Json parts = Json::object();
        for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i)
          parts[scene.at("visualParts")[i].at("visualPartId").get<std::string>()] =
            std::vector<double>(pose.isometries().begin() + i * 16, pose.isometries().begin() + (i + 1) * 16);
        sample[std::to_string(static_cast<int>(progress * 100))] = std::move(parts);
      }
      frames.push_back(std::move(sample));
    }
    const auto saved = session.save();
    Session restored(session.execution()); restored.lightweight_results(true);
    if (restored.load(saved).at("status") != "Loaded" || restored.state() != session.state())
      throw std::runtime_error("Artifact replay differs.");
    const auto final_state = session.state();
    if (session.undo(session.revision()).at("status") != "Committed" ||
        session.redo(session.revision()).at("status") != "Committed" || session.state() != final_state)
      throw std::runtime_error("Artifact undo/redo differs.");
    std::cout << Json{{"artifactInfo", artifact::info()}, {"initial", initial}, {"result", result},
      {"save", saved}, {"frames", frames}, {"parts", scene.at("visualParts").size()}}.dump() << '\n';
  } catch (const std::exception &error) {
    std::cerr << diagnostic_result(error).dump() << '\n'; return 1;
  }
}
