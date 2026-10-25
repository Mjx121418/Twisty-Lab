#include "twisty/kernel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>

using namespace twisty;
namespace {
std::string read(const std::string &path) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + path);
  if (!file)
    throw std::runtime_error("Missing " + path);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}
void check(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  try {
    action();
  } catch (const DiagnosticError &e) {
    check(diagnostic_result(e).at("diagnostics").at(0).at("reasonCode") == "realization.articulated",
          "Incorrect shared-track diagnostic");
    return;
  }
  throw std::runtime_error("Invalid shared track was accepted");
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string family = argc > 1 ? argv[1] : "a";
    check(family.size() == 1 && std::string("abcd").find(family) != std::string::npos, "Unknown Melty family");
    const auto id = "melty-" + family;
    const auto directory = "packages/" + id + "/";
    const auto execution = Execution::from_source(read(directory + "package.json"));
    const auto definition = execution->definition();
    const auto manifest = Json::parse(read(directory + "manifest.json"));
    const auto seed = Json::parse(read("scripts/data/" + id + "-middle-catalog.json"));
    const auto witnesses = Json::parse(read("scripts/data/" + id + "-witnesses.json"));
    const std::string labels = "URFLBD";
    std::map<std::string, std::vector<std::string>> tuples;
    const auto &source = definition->canonical.at("source");
    for (const auto &request : source.at("requests")) {
      auto &tuple = tuples[request.at("id").get<std::string>()];
      for (const auto &domain : source.at("domains"))
        tuple.push_back(request.at("profiles").at(0).at("choices").at(domain.at("id").get<std::string>()));
    }
    std::vector<Index> pieces;
    for (const auto &p : seed.at("pieces")) {
      const auto size = p.at("caps").size();
      const auto id = (size == 1   ? "center/"
                       : size == 2 ? "edge/"
                       : size == 3 ? "corner/"
                                   : "") +
                      p.at("id").get<std::string>();
      pieces.push_back(static_cast<Index>(std::find_if(definition->pieces.begin(), definition->pieces.end(),
                                                       [&](const auto &piece) { return piece.id == id; }) -
                                          definition->pieces.begin()));
      check(pieces.back() < definition->pieces.size(), "Missing Melty piece");
    }
    std::array<Index, 6> centers;
    for (Index i = 0; i < 6; ++i)
      centers[i] = static_cast<Index>(
          std::find_if(definition->pieces.begin(), definition->pieces.end(),
                       [&](const auto &piece) { return piece.id == "center/" + labels.substr(i, 1); }) -
          definition->pieces.begin());
    std::vector<State> states;
    std::vector<std::vector<std::string>> operations;
    states.reserve(witnesses.at("nodes").size());
    operations.reserve(witnesses.at("nodes").size());
    std::optional<CompletedTransition> sample;
    Index sample_node = 0;
    std::size_t split_nodes = 0, primitive_steps = 0;
    for (const auto &node : witnesses.at("nodes")) {
      const int parent = node.at(0);
      if (parent < 0) {
        states.push_back(definition->initial);
        operations.emplace_back();
        continue;
      }
      const auto &before = states.at(parent);
      auto state = before;
      std::vector<std::string> steps;
      for (const auto &[g, t] : {std::pair<int, int>{node.at(1), node.at(3)}, {node.at(2), node.at(4)}}) {
        if (g < 0)
          continue;
        const auto &angles = manifest.at("phaseAnglesDegrees").at(labels.substr(g, 1));
        const double angle = std::remainder(
            angles.at(t).get<double>() - angles.at(before.placement_of[centers[g]]).get<double>(), 360.);
        std::string local;
        for (const auto &[name, value] : manifest.at("turnAnglesDegrees").items())
          if (name[0] == labels[g] && std::abs(value.get<double>() - angle) < 1e-8)
            local = name;
        check(!local.empty(), "Unresolved witness phase");
        // Each old paired witness must now pass through a legal intermediate
        // state. Supply complete tuples independently of default completion.
        const auto plan = execution->plan(state, local, tuples.at(local));
        check(plan.transition.has_value(),
              "Rejected reachable witness " + std::to_string(states.size()) + ": " + local);
        if (states.size() % 128 == 0) {
          const auto inverse = execution->inverse(*plan.transition);
          check(inverse.transition && inverse.transition->after == state,
                "Melty inverse changed the complete state");
        }
        state = plan.transition->after;
        steps.push_back(local);
        ++primitive_steps;
        if (node.at(2).get<int>() >= 0 && !sample) {
          sample = plan.transition;
          sample_node = static_cast<Index>(states.size());
        }
      }
      if (steps.size() == 2) {
        // Opposite cap sweeps are disjoint; both orders have the same endpoint.
        auto reversed = before;
        for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
          auto plan = execution->plan(reversed, *it);
          check(plan.transition.has_value(), "Reversed independent cap is blocked");
          reversed = plan.transition->after;
        }
        check(reversed == state, "Opposite cap order changed the state");
        ++split_nodes;
      }
      states.push_back(std::move(state));
      operations.push_back(std::move(steps));
    }
    std::set<std::pair<Index, Index>> covered;
    for (const auto &w : witnesses.at("witnesses")) {
      const Index p = w.at(0), a = w.at(1), node = w.at(2);
      check(states.at(node).placement_of[pieces[p]] == a, "Catalog witness differs from compiled transport");
      covered.emplace(p, a);
    }
    std::set<std::pair<Index, Index>> expected;
    for (Index p = 0; p < seed.at("pieces").size(); ++p) {
      const auto &placements = seed.at("pieces").at(p).at("placements");
      for (Index a = 0; a < placements.size(); ++a)
        if (placements.at(a).value("reachabilityWitnessed", true))
          expected.emplace(p, a);
    }
    const Index total = manifest.at("placementCount");
    const Index unclassified = manifest.at("unclassifiedPlacements").size();
    check(covered == expected && covered.size() == total - unclassified && sample.has_value(),
          "Incomplete placement/split witness coverage");
    check(manifest.at("stopRule") == "middle-aligned-single-caps", "Wrong local stop rule");
    Controller controls(execution);
    check(controls.catalog().at("families").size() == 6, "Melty should expose six independent families");
    for (const auto &op : definition->operations)
      check(op.id.find('+') == std::string::npos, "Retained a redundant paired request");
    states.clear();
    Session session(execution), restored(execution);
    session.lightweight_results(true);
    const auto initial = session.state();
    std::vector<Index> nodes;
    for (Index node = sample_node; node != 0; node = witnesses.at("nodes").at(node).at(0))
      nodes.push_back(node);
    std::reverse(nodes.begin(), nodes.end());
    std::vector<std::string> path;
    for (const auto node : nodes)
      path.insert(path.end(), operations.at(node).begin(), operations.at(node).end());
    for (const auto &op : path)
      check(session.execute(op, session.revision()).at("status") == "Committed", "Independent cap path failed");
    check(restored.load(session.save()).at("status") == "Loaded" && restored.state() == session.state(),
          "Melty replay differs");
    for (std::size_t i = 0; i < path.size(); ++i)
      check(session.undo(session.revision()).at("status") == "Committed", "Melty undo failed");
    check(session.state() == initial, "Melty undo did not restore solved");
    std::string prefix;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
      prefix += (i ? " " : "") + path[i];

    for (const auto *name : {"euclidean", "port-diagram", "spherical"}) {
      const auto text = read(directory + id + "-" + name + ".json");
      GeometryDefinition geometry(execution, text);
      const auto record = execution->encode(*sample).dump();
      const auto animation = geometry.animate(record);
      const auto target = geometry.frame(encode_state(*definition, sample->after).dump());
      check(animation.evaluate(1).isometries() == target.isometries(), "Single-cap geometry endpoint differs");
      for (auto x : animation.evaluate(.5).isometries())
        check(std::isfinite(x), "Nonfinite single-cap animation");
      if (family != "a")
        continue;
      auto model = Json::parse(text);
      Json table=Json::object();
      const auto d=static_cast<Index>(std::find_if(definition->domains.begin(),definition->domains.end(),
        [](const auto &d){return d.id=="URF";})-definition->domains.begin());
      for(const auto &op:definition->operations) {
        auto &entries=table[op.id]=Json::object();
        for(const auto &region:model.at("operationTrackTables").at("caps").at(op.id).at("regions")) {
          const auto &support=definition->operations.at(definition->operation_ids.at(region.at("request")));
          for(Index q=0;q<definition->domains[d].placements.size();++q) if(support.roles[d][q]==1)
            entries[definition->domains[d].placements[q].key]=model.at("rotationTracks").at(region.at("path"));
        }
      }
      model["componentsByDomain"]["URF"][0]["operationTracks"] = table;
      GeometryDefinition expanded(execution, model.dump());
      check(expanded.animate(record).evaluate(.5).isometries() == animation.evaluate(.5).isometries(),
            "Shared and explicit paths differ");
      model["componentsByDomain"]["URF"][0]["operationTracks"] = "missing";
      rejects([&] { GeometryDefinition bad(execution, model.dump()); });
      model["componentsByDomain"]["URF"][0]["operationTracks"] = "caps";
      model["operationTrackTables"]["caps"]["U"]["regions"][0]["path"] = "missing";
      rejects([&] { GeometryDefinition bad(execution, model.dump()); });
      model["operationTrackTables"]["caps"]["U"]["regions"][0]["path"] = "U";
      model["rotationTracks"]["U"]["rate"] = .1;
      rejects([&] { GeometryDefinition bad(execution, model.dump()); });
    }
    std::cout << id << ": " << covered.size() << " witnessed placements, " << unclassified
              << " unclassified memberships, " << operations.size()
              << " compiled witness nodes, " << split_nodes << " split paired nodes, " << primitive_steps
              << " single turns, replay and shared tracks passed\n";
    std::cout << Json{{"family", family}, {"prefix", prefix}, {"operation", path.back()},
                      {"definitionDigest", definition->digest}}.dump() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
