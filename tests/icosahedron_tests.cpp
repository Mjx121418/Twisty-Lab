#include "twisty/kernel.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
namespace {
std::string read(const std::string &name) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + name);
  if (!file) throw std::runtime_error("Missing " + name);
  std::ostringstream text; text << file.rdbuf(); return text.str();
}
void check(bool ok, const std::string &message) { if (!ok) throw std::runtime_error(message); }
}

int main() {
  try {
    for (const auto count : {110,152}) {
      const auto directory = "packages/icosahedron-" + std::to_string(count) + "/";
      const auto execution = Execution::from_source(read(directory+"package.json"));
      const auto definition = execution->definition();
      check(definition->pieces.size() == static_cast<std::size_t>(count) && definition->operations.size() == 280, "Incorrect piece or movement catalog");
      std::size_t placements = 0;
      for (const auto &domain : definition->domains) placements += domain.placements.size();
      check(placements == (count == 110 ? 1080 : 2880), "Incorrect shared pose catalog");
      Session session(execution), restored(execution);
      session.lightweight_results(true); restored.lightweight_results(true);
      const auto origin = session.state();
      if (count == 110) {
        Controller controller(execution);
        check(controller.catalog().at("families").size() == 20, "Expected twenty axes");
        check(controller.families_for_pieces(session, {"center/A"}).at("families") == Json::array({"A"}), "Center selects the wrong axis");
        check(controller.plan_to(session, {{"family", "A"}, {"targets", {{"A", "1"}}}}, session.revision()).at("status") == "Planned", "Initial jumble destination is unavailable");
      }
      check(session.execute("A_j", session.revision()).at("status") == "Committed", "First jumble failed");
      check(session.execute("I", session.revision()).at("status") == "Blocked", "Jumbled cut crossed a piece");
      if (count == 152) {
        check(execution->plan(session.state(), "D").transition.has_value(), "Spherical D turn was unexpectedly rejected");
      }
      check(session.execute("O_j", session.revision()).at("status") == "Committed", "Intersecting jumble handoff failed");
      check(restored.load(session.save()).at("status") == "Loaded" && restored.state() == session.state(), "Saved jumble did not replay");
      check(session.undo(session.revision()).at("status") == "Committed", "Second jumble undo failed");
      check(session.undo(session.revision()).at("status") == "Committed" && session.state() == origin, "Jumble undo did not restore solved");
      check(session.run("A_j O_j (A_j O_j)'", "transactional", session.revision()).at("status") == "Committed" && session.state() == origin, "Algorithm inverse differs");
      const auto planned = execution->plan(origin, "A_j");
      check(planned.transition.has_value(), "Missing geometric witness");
      const auto record = execution->encode(*planned.transition).dump();
      for (const auto *kind : {"euclidean", "spherical"}) {
        GeometryDefinition geometry(execution, read(directory + "icosahedron-" + std::to_string(count) + "-" + kind + ".json"));
        auto animation = geometry.animate(record);
        auto target = geometry.frame(encode_state(*definition, planned.transition->after).dump());
        check(animation.evaluate(1).isometries() == target.isometries(), "Geometric endpoint differs");
        for (auto value : animation.evaluate(.5).isometries()) check(std::isfinite(value), "Nonfinite animation frame");
      }
    }
    std::cout << "Icosahedra 110 and 152: jumbling, blocking, controls, replay, inverse and both geometric views passed\n";
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
