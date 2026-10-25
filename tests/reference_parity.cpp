#include "twisty/kernel.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

using namespace twisty;
namespace {
std::string read(const std::string &path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("Missing " + path);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}
void check(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
Json snapshot(const Json &bundle) {
  const auto puzzle = language::compile(bundle.at("source"));
  const auto &program = puzzle.program;
  const auto &d = program.data();
  const auto &b = puzzle.bindings;
  Json maps = Json::object(), moves = Json::array();
  std::vector<core::Index> roots;
  for (const auto &domain : b.move_maps)
    for (auto map : domain)
      if (map != core::no_index) roots.push_back(map);
  // Query every global placement, not just participating support entries.
  // Store sparse results; the catalog size fixes every absent row as well.
  std::map<core::Index, std::string> map_hashes;
  for (auto id : roots) {
    if (map_hashes.contains(id)) continue;
    Json rows = Json::array();
    for (core::Index q = 0; q < d.placement_count; ++q)
      if (auto to = program.transport(id, q)) rows.push_back({q, *to});
    map_hashes[id] = sha256(rows.dump());
    maps[map_hashes[id]] = std::move(rows);
  }
  for (const auto &domain : b.move_maps) {
    auto row = Json::array();
    for (auto map : domain) row.push_back(map == core::no_index ? Json(nullptr) : Json(map_hashes.at(map)));
    moves.push_back(row);
  }
  std::vector<std::string> predicates(d.predicates.size());
  const auto predicate = [&](auto &&self, core::Ref r) -> std::string {
    if (r < 2) return r ? "true" : "false";
    const auto i = r - 2;
    if (predicates[i].empty()) {
      const auto &p = d.predicates[i];
      Json value;
      if (p.opcode == core::Opcode::in)
        value = {"IN", int(p.bank), p.input, d.finite_sets[p.set]};
      else {
        auto children = Json::array();
        for (core::Index k = 0; k < p.child_count; ++k)
          children.push_back(self(self, d.child_references[p.first_child + k]));
        value = {"RANGE", p.lower, p.upper, children};
      }
      predicates[i] = sha256(value.dump());
    }
    return predicates[i];
  };
  Json requests = Json::array();
  for (std::size_t i = 0; i < d.requests.size(); ++i) {
    const auto &r = d.requests[i];
    const auto &binding = b.requests[i];
    auto actions = Json::array(), profiles = Json::array();
    for (core::Index k = 0; k < r.action_count; ++k) {
      const auto &a = d.actions[r.first_action + k];
      actions.push_back({a.piece, map_hashes.at(a.map), predicate(predicate, a.selection), a.event});
    }
    for (const auto &p : binding.profiles)
      profiles.push_back({p.id, p.choices, predicate(predicate, p.when)});
    requests.push_back(sha256(Json{binding.id, r.choice_sizes, predicate(predicate, r.relation), actions,
                        profiles, binding.default_profile ? Json(*binding.default_profile) : Json(nullptr)}.dump()));
  }
  Json semantics = {{"pieces", b.pieces}, {"placements", b.placements}, {"events", b.events},
                    {"choiceSlots", b.choice_slots}, {"choiceValues", b.choice_values},
                    {"initial", d.initial}, {"invariant", predicate(predicate, d.invariant)},
                    {"goal", predicate(predicate, d.goal)}, {"requests", requests}};
  std::vector<double> samples;
  std::uint64_t checksum = 0;
  constexpr unsigned iterations = 500000;
  for (int sample = 0; sample < 3; ++sample) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned k = 0; k < iterations; ++k) {
      auto to = program.transport(roots[k % roots.size()], (std::uint64_t(k) * 131) % d.placement_count);
      checksum += to ? *to + 1 : 0;
    }
    samples.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now()-start).count()/iterations);
  }
  std::sort(samples.begin(), samples.end());
  return {{"semantics", semantics}, {"maps", maps}, {"moveMaps", moves},
          {"storageBytes", program.transport_storage_bytes()}, {"lookupNanoseconds", samples[1]},
          {"lookupChecksum", checksum}};
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 3, "Expected --record PACKAGE or --check ORACLE");
    const auto input = Json::parse(read(argv[2]));
    if (std::string(argv[1]) == "--record") {
      auto out = snapshot(input);
      out["package"] = input;
      out["digest"] = Execution::from_source(input.dump())->definition()->digest;
      std::cout << out.dump() << '\n';
    } else {
      check(std::string(argv[1]) == "--check", "Unknown mode");
      // Exercise the legacy loader and canonical serialization independently.
      {
        auto old = Execution::from_source(input.at("package").dump());
        check(old->definition()->digest == input.at("digest"), "Old format fingerprint changed");
        check(Execution::from_source(old->definition_json().dump())->definition()->digest == input.at("digest"),
              "Old definition did not round trip");
      }
      auto current = Json::parse(read("packages/" + input.at("name").get<std::string>() + "/package.json"));
      const auto actual = snapshot(current);
      for (const auto *key : {"semantics", "maps", "moveMaps", "lookupChecksum"})
        check(actual.at(key) == input.at(key), std::string("Changed ") + key + " for " + input.at("name").get<std::string>());
      std::cout << Json{{"name", input.at("name")}, {"beforeBytes", input.at("storageBytes")},
                        {"afterBytes", actual.at("storageBytes")}, {"beforeLookupNs", input.at("lookupNanoseconds")},
                        {"afterLookupNs", actual.at("lookupNanoseconds")}}.dump() << '\n';
    }
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
