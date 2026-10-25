#include "twisty/kernel.hpp"
#include <cmath>
#include <array>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>

using namespace twisty;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream f(std::string(PROJECT_ROOT) + "/" + path);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}
} // namespace
int main() {
  try {
    auto package = Json::parse(read("packages/helicopter/package.json"));
    auto e = Execution::from_source(package.dump());
    Session s(e);
    Controller c(e);
    check(c.catalog().at("families").size() == 12, "Expected twelve grip controls");
    check(c.family_state(s, "UF").at("current").at("UF") == "a", "Initial phase differs");
    auto intent = Json{{"family", "UF"}, {"targets", {{"UF", "d"}}}};
    auto before = s.save();
    auto preview = c.plan_to(s, intent, "0");
    check(preview.at("status") == "Planned", "Half-turn preview failed");
    check(s.save() == before, "Preview mutated session");
    auto result = c.turn_to(s, intent, "0");
    check(result.at("status") == "Committed", "Half turn failed");
    check(!result.at("snapshot").contains("legalRequests"), "Commit enumerated global requests");
    check(c.family_state(s, "UF").at("current").at("UF") == "d", "Marked half turn lost phase");
    check(c.turn_to(s, intent, "0").at("status") == "StaleRevision", "Stale commit accepted");
    check(c.turn_to(s, intent, "1").at("status") == "NoOp", "Current stop should be no-op");
    check(s.revision() == "1", "Rejected/no-op command changed revision");
    auto page = c.destinations(s, "UF", 0, 2);
    check(page.at("destinations").size() == 2 && page.at("total") == 6, "Preview pagination failed");
    check(c.turn_to(s, {{"family", "UF"}, {"targets", {{"UF", "a"}}}}, s.revision()).at("status") ==
              "Committed",
          "Lifted source route failed");
    check(s.execution()->is_solved(s.state()), "Marked mechanism created physical goal constraint");
    s.undo(s.revision());
    check(c.family_state(s, "UF").at("current").at("UF") == "d", "Undo phase differs");
    s.redo(s.revision());
    check(c.family_state(s, "UF").at("current").at("UF") == "a", "Redo phase differs");
    Session loaded(e);
    loaded.load(s.save());
    check(loaded.state() == s.state(), "Phase replay differs");
    const auto controls = package.at("catalog").at("controls");
    for (const auto &family : controls.at("families")) {
      auto id = family.at("id").get<std::string>();
      // Every authored request's target and inverse are checked wherever legal.
      for (const auto &route : family.at("routes")) {
        auto state = e->definition()->initial;
        const auto piece = "mechanism/" + id;
        Index p = 0;
        while (e->definition()->pieces[p].id != piece)
          ++p;
        auto &domain = e->definition()->domains[e->definition()->pieces[p].domain];
        state.placement_of[p] = domain.by_key.at("edge/" + id + "/" + route.at("from")[0].get<std::string>());
        // A single changed phase may violate solid collisions; such states are not fixtures.
        try {
          e->validate(state);
        } catch (const DiagnosticError &err) {
          if (err.code != "state.invariant")
            throw;
          continue;
        }
        auto plan = c.plan_to(state, {{"family", id}, {"targets", {{id, route.at("to")[0]}}}});
        if (plan.at("status") == "Planned") {
          auto target = e->decode(plan.at("transition").at("afterState"));
          check(c.family_state(target, id).at("current").at(id) == route.at("to")[0],
                "Route phase target differs");
          auto inverse = e->plan(
              target,
              e->definition()->operations[e->definition()->operation_ids.at(route.at("request"))].inverse);
          check(inverse.transition && inverse.transition->after == state, "Lifted inverse differs");
        }
      }
    }
    for (const auto &mutation : {"piece", "stop", "request", "duplicate"}) {
      auto bad = package;
      auto &controls = bad["catalog"]["controls"];
      if (std::string(mutation) == "piece")
        controls["drivers"][0]["piece"] = "unknown";
      if (std::string(mutation) == "stop")
        controls["drivers"][0]["stops"][1] = controls["drivers"][0]["stops"][0];
      if (std::string(mutation) == "request")
        controls["families"][0]["routes"][0]["request"] = "missing";
      if (std::string(mutation) == "duplicate")
        controls["families"][0]["routes"].push_back(controls["families"][0]["routes"][0]);
      bool rejected = false;
      try {
        Execution::from_source(bad.dump());
      } catch (const DiagnosticError &err) {
        rejected = err.code == "controls.invalid";
      }
      check(rejected, "Malformed control catalog accepted");
    }
    check(Json::parse(c.turn_to_json(s, R"({"family":"UF","targets":{"UF":"missing"}})", s.revision()))
                  .at("status") == "Invalid",
          "Unknown stop accepted");
    auto alternative = package;
    auto &routes = alternative["catalog"]["controls"]["families"][0]["routes"];
    auto half = std::find_if(routes.begin(), routes.end(),
                             [](const auto &r) { return r.at("from")[0] == "a" && r.at("to")[0] == "d"; });
    auto second = *half;
    second["pathId"] = "alternative";
    routes.push_back(second);
    auto other = Execution::from_source(alternative.dump());
    Controller a(other);
    const auto &f = alternative["catalog"]["controls"]["families"][0];
    Json choice = {{"family", f.at("id")}, {"targets", {{f.at("id").get<std::string>(), "d"}}}};
    check(a.plan_to(other->definition()->initial, choice).at("status") == "ChoiceRequired",
          "Ambiguous path silently selected");
    choice["pathId"] = "alternative";
    check(a.plan_to(other->definition()->initial, choice).at("status") == "Planned",
          "Explicit path selection failed");
    auto realization = Json::parse(read("packages/helicopter/helicopter-euclidean.json"));
    auto legacy = realization;
    legacy.erase("motionFamilies");
    auto &caps = legacy["requiredCapabilities"];
    caps.erase(std::remove(caps.begin(), caps.end(), "family-rotation-generators"), caps.end());
    for (auto &[op, track] : legacy["operationTracks"].items()) {
      const auto family = track.at("family").get<std::string>();
      const auto axis = realization["motionFamilies"]["families"][family]["axis"];
      track = {{"axis", axis}, {"fromStop", op[op.size() - 2] - 'a'}, {"toStop", op.back() - 'a'}};
    }
    Geometry generated(e, realization.dump()), original(e, legacy.dump());
    for (const auto &op : {"UF_ab", "UF_ad", "UF_ae"}) {
      const auto move = e->plan(e->definition()->initial, op);
      check(move.transition.has_value(), "Motion fixture blocked");
      const auto record = e->encode(*move.transition).dump();
      generated.prepare_animation_json(record);
      original.prepare_animation_json(record);
      for (double t : {0., .25, .5, .75, 1.}) {
        generated.sample(t);
        original.sample(t);
        check(generated.transforms().size() == original.transforms().size(), "Generator part count differs");
        for (std::size_t k = 0; k < generated.transforms().size(); ++k)
          check(std::abs(generated.transforms()[k] - original.transforms()[k]) < 1e-6,
                "Generator changed signed path");
      }
    }
    for (const auto &badKind : {"winding", "axis", "capability"}) {
      auto bad = realization;
      if (std::string(badKind) == "winding")
        bad["operationTracks"]["UF_ab"]["winding"] = 1000000;
      if (std::string(badKind) == "axis")
        bad["motionFamilies"]["families"]["UF"]["axis"] = {0, 0, 0};
      if (std::string(badKind) == "capability")
        bad["requiredCapabilities"].push_back("unknown");
      bool rejected = false;
      try {
        Geometry invalid(e, bad.dump());
      } catch (const DiagnosticError &) {
        rejected = true;
      }
      check(rejected, "Malformed generator accepted");
    }
    const auto fixtures = Json::parse(read("tests/fixtures/core.json"));
    for (const auto &fixture : fixtures.at("cases")) {
      if (!fixture.at("name").get<std::string>().ends_with("opposite caps execute separately"))
        continue;
      auto execution = Execution::from_source(read(fixture.at("source")));
      Controller control(execution);
      check(control.catalog().at("families").size() == 6, "Redundant paired Melty family");
      const auto prefix = fixture.at("prefix").get<std::string>();
      const auto split = prefix.rfind(' ');
      const std::string first = prefix.substr(split + 1), second = fixture.at("operation");
      std::optional<State> expected;
      for (const auto &steps : {std::array{first, second}, std::array{second, first}}) {
        Session caps(execution);
        caps.lightweight_results(true);
        check(caps.run(prefix.substr(0, split), "transactional", caps.revision()).at("status") == "Committed",
              "Independent cap prefix failed");
        for (const auto &op : steps) {
          const auto plan = execution->plan(caps.state(), op);
          check(plan.transition.has_value(), "Independent cap fixture is blocked");
          const auto family = execution->definition()->operations[execution->definition()->operation_ids.at(op)].family;
          const auto targets = control.family_state(plan.transition->after, family).at("current");
          const auto cursor = caps.snapshot(SnapshotDetail::StateOnly).at("cursor").get<int>();
          const auto result = control.turn_to(caps, {{"family", family}, {"targets", targets}}, caps.revision());
          check(result.at("status") == "Committed" && caps.state() == plan.transition->after,
                "Single-cap controller target differs");
          check(result.at("transitions").size() == 1 && result.at("snapshot").at("cursor") == cursor + 1,
                "Independent cap must have one history entry");
          check(result.at("transitions")[0] == execution->encode(*plan.transition),
                "Single-cap events/choices differ");
        }
        if (expected) check(*expected == caps.state(), "Opposite cap order changed controller endpoint");
        expected = caps.state();
      }
    }
    std::cout << checks << " controller checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
