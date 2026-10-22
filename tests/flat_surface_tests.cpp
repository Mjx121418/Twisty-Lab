#include "twisty/kernel.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>

using namespace twisty;
namespace {
void check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream input(std::string(PROJECT_ROOT) + "/" + path);
  check(bool(input), "Missing " + path);
  std::ostringstream out; out << input.rdbuf(); return out.str();
}
void reject(const std::shared_ptr<const Execution> &execution, const Json &realization) {
  try { Geometry invalid(execution, realization.dump()); }
  catch (const DiagnosticError &) { return; }
  throw std::runtime_error("Malformed flat realization was accepted");
}
void puzzle(const std::string &topology, bool hex = false) {
  const auto directory = std::string("packages/flat-") + (hex ? "hex-" : "") + topology + "/";
  Session session(read(directory + "package.json"));
  const auto document = Json::parse(read(directory + "flat-quotient.json"));
  Geometry geometry(session.execution(), document.dump());
  const auto scene = Json::parse(geometry.scene_json());
  check(scene.at("ambientSpace") == "E2", "Not a flat chart");
  check(scene.at("flatSurface").at("topology") == topology, "Wrong topology");
  check(scene.at("flatSurface").at("deckGenerators").size() == 2, "Missing identifications");
  check(scene.at("visualParts").size() == (hex ? 100 : 116), "Interpreter must emit physical parts once");
  check(session.definition()->pieces.size() == (hex ? 24 : 32), "Wrong piece count");
  check(session.definition()->operations.size() == (hex ? 20 : 12), "Missing turns");
  if (hex) {
    check(scene.at("flatSurface").at("fundamentalPolygon").size() == 4, "Missing rhombus boundary");
    check(scene.at("flatSurface").contains("tileReflection") == (topology == "klein-bottle"), "Wrong tile holonomy");
  }
  const auto origin = session.snapshot().at("stateDigest");
  for (const std::string name : {"A", "B", "C", "D"}) {
    for (const auto &algorithm : {name + " " + name + "'", "(" + name + (hex ? ")6" : ")4"),
                                 name + (hex ? "3 " : "2 ") + name + (hex ? "3" : "2"),
                                 name + "2 " + name + (hex ? "2'" : "2")}) {
      const auto result = session.run(algorithm, "transactional", session.revision());
      check(result.at("status") == "Committed", "Turn rejected");
      for (const auto &record : result.at("transitions")) {
        check(Json::parse(geometry.prepare_animation_json(record.dump())).at("status") == "Prepared",
              "Transition could not be interpreted");
        for (double t : {0., .1, .5, .9, 1.}) {
          geometry.sample(t);
          for (float value : geometry.transforms()) check(std::isfinite(value), "Nonfinite frame");
        }
      }
      check(session.snapshot().at("stateDigest") == origin, "Turn inverse/order failed");
    }
  }
  const auto result = session.run("A B C' D A' B2 C D'", "transactional", session.revision());
  check(result.at("status") == "Committed", "Mixed word failed");
  for (const auto &record : result.at("transitions")) {
    check(Json::parse(geometry.prepare_animation_json(record.dump())).at("status") == "Prepared", "Track failed");
    geometry.sample(1);
  }
  const auto exact = geometry.transforms();
  geometry.set_state_json(session.state_json());
  check(geometry.transforms() == exact, "Resting state differs from sampled endpoint");
  auto witness = result.at("transitions").front();
  witness["pieceActions"][0]["event"] = "invented";
  check(Json::parse(geometry.prepare_animation_json(witness.dump())).at("status") == "UnsupportedTransition",
        "Forged event was accepted");
  check(geometry.transforms() == exact, "Rejected witness changed frame");
  check(Json::parse(geometry.bind_hit_json(scene.at("visualParts")[0].at("visualPartId").get<std::string>())).at("status") == "Target",
        "Missing intrinsic pick binding");

  // Reachability, rather than merely the presence of mirrored authored poses:
  // an edge can acquire reversed chart handedness only on the Klein bottle.
  const auto &definition = *session.definition();
  const auto d = std::find_if(definition.domains.begin(), definition.domains.end(), [](const auto &d) { return d.id == "edge"; });
  const auto domain = static_cast<Index>(d - definition.domains.begin());
  const auto piece = std::find_if(definition.pieces.begin(), definition.pieces.end(), [&](const auto &p) { return p.domain == domain; });
  std::set<Index> reached{piece->home}; std::queue<Index> queue; queue.push(piece->home);
  while (!queue.empty()) { const auto q = queue.front(); queue.pop();
    for (const auto &operation : definition.operations) {
      const auto target = operation.maps[domain][q];
      if (reached.insert(target).second) queue.push(target);
    }
  }
  const auto &frames = document.at("componentsByDomain").at("edge")[0].at("placementFrames");
  bool reflected = false;
  for (auto q : reached) reflected |= frames.at(d->placements[q].key)[10].get<double>() < 0;
  check(reflected == (topology == "klein-bottle"), "Incorrect quotient holonomy");

  auto malformed = document; malformed["flatSurface"]["topology"] = "sphere"; reject(session.execution(), malformed);
  malformed = document; malformed["assets"]["face/body"]["vertices"][0][2] = .1; reject(session.execution(), malformed);
  malformed = document;
  auto &frame = malformed["componentsByDomain"]["face"][0]["placementFrames"].begin().value();
  frame[12] = 5; reject(session.execution(), malformed);
  malformed = document;
  auto &track = malformed["componentsByDomain"]["face"][0]["operationTracks"]["A"].begin().value();
  track[0]["angle"] = .1; reject(session.execution(), malformed);
  malformed = document; malformed["flatSurface"]["chart"] = "skew"; reject(session.execution(), malformed);
  if (hex && topology == "klein-bottle") {
    malformed = document; malformed["flatSurface"]["cutRadius"] = .9; reject(session.execution(), malformed);
  }
  std::cout << (hex ? "hexagonal " : "") << topology << ": exact transports, quotient holonomy, frames and rejection checks passed\n";
}

void pillowcase(bool six) {
  const std::string folder = std::string("packages/flat-pillowcase") + (six ? "-6/" : "/");
  Session session(read(folder + "package.json"));
  const auto document = Json::parse(read(folder + "flat-quotient.json"));
  Geometry geometry(session.execution(), document.dump());
  const auto scene = Json::parse(geometry.scene_json());
  const auto &surface = scene.at("flatSurface");
  check(surface.at("topology") == "pillowcase", "Wrong orbifold topology");
  check(surface.at("deckGenerators").size() == 3, "Pillowcase requires its involution");
  check(surface.at("conePoints").size() == 4, "Missing singularities");
  check(surface.at("fundamentalPolygon").size() == 4 && surface.contains("tileHolonomy"),
        "Missing square orbifold chart");
  check(session.definition()->pieces.size() == (six ? 34 : 18), "Wrong pillowcase piece count");
  check(scene.at("visualParts").size() == (six ? 122 : 64), "Duplicate physical parts");
  const auto origin = session.snapshot().at("stateDigest");
  const std::string names = six ? "ABCDEF" : "ABCD";
  for (char name : names) {
    const std::string face(1, name);
    for (const auto &word : {face + " " + face + "'", "(" + face + ")" + (name <= 'D' ? "2" : "4"),
                             face + "2 " + face + "2"}) {
      const auto result = session.run(word, "transactional", session.revision());
      check(result.at("status") == "Committed", "Orbifold turn failed");
      for (const auto &record : result.at("transitions")) {
        check(Json::parse(geometry.prepare_animation_json(record.dump())).at("status") == "Prepared",
              "Orbifold track rejected");
        for (double t : {0., .001, .25, .5, .999, 1.}) {
          geometry.sample(t);
          for (float v : geometry.transforms()) check(std::isfinite(v), "Nonfinite orbifold frame");
        }
      }
      check(session.snapshot().at("stateDigest") == origin, "Incorrect orbifold move order");
    }
  }
  const std::string word = six ? "A B E F C D E2 F'" : "A B C D A' C B";
  const auto mixed = session.run(word + " (" + word + ")'", "transactional", session.revision());
  check(mixed.at("status") == "Committed", "Mixed orbifold word failed");
  for (const auto &record : mixed.at("transitions")) {
    check(Json::parse(geometry.prepare_animation_json(record.dump())).at("status") == "Prepared",
          "Mixed orbifold track failed");
    geometry.sample(.37);
    geometry.sample(1);
  }
  check(session.snapshot().at("stateDigest") == origin, "Mixed word did not restore the orbifold");
  const auto exact = geometry.transforms();
  geometry.set_state_json(session.state_json());
  check(geometry.transforms() == exact, "Orbifold endpoint differs from exact resting state");
  auto malformed = document;
  malformed["flatSurface"]["cutRadius"] = six ? .71 : 1.;
  reject(session.execution(), malformed);
  malformed = document;
  malformed["flatSurface"]["chart"] = "square";
  reject(session.execution(), malformed);
  malformed = document;
  malformed["flatSurface"]["diskCenters"]["near-cone"] = {.51,.5};
  reject(session.execution(), malformed);
  malformed = document;
  malformed["requiredCapabilities"].erase(malformed["requiredCapabilities"].end() - 1);
  reject(session.execution(), malformed);
  std::cout << (six ? "six" : "four") << "-disk pillowcase: orders, quotient tracks and singularity bounds passed\n";
}
} // namespace
int main() {
  try {
    puzzle("torus"); puzzle("klein-bottle"); puzzle("torus", true); puzzle("klein-bottle", true);
    pillowcase(false); pillowcase(true);
  }
  catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
