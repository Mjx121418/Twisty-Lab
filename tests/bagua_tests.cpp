#include "twisty/geometry.hpp"
#include "twisty/session.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace twisty;
namespace {
int checks = 0;
void check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
std::string read(const std::string &path) {
  std::ifstream input(std::string(PROJECT_ROOT) + "/" + path);
  if (!input)
    throw std::runtime_error("Cannot read " + path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}
State run(const Definition &d, State state, const std::string &algorithm) {
  for (const auto &operation : parse_notation(d, algorithm)) {
    const auto next = plan(d, state, operation);
    check(next.transition.has_value(),
          "Published sequence blocked at " + operation + ": " + next.blocked.dump());
    state = next.transition->after;
  }
  return state;
}
void model_checks(const std::string &source) {
  Session session(source);
  const auto d = session.definition();
  const auto review = Json::parse(read("packages/bagua/review.json"));
  check(d->digest == review.at("definitionDigest").get<std::string>(), "Review pins the exact rule model");
  check(d->placement_relations && d->pieces.size() == 146 && d->operations.size() == 30,
        "Bagua uses existing placement relations with 146 pieces and 30 turns");
  std::map<std::string, int> counts;
  for (const auto &piece : d->pieces)
    ++counts[piece.type];
  check(counts == review.at("pieceCounts").get<std::map<std::string, int>>(),
        "Physical piece counts agree with the specification");
  check(validate_state(*d, d->initial).empty(), "The solved assignment is valid");
  for (const auto &face : {"U", "D", "R", "L", "F", "B"}) {
    const std::string f = face;
    check(run(*d, d->initial, f + "+8") == d->initial, "Eight 45-degree turns restore labeled centers too");
    check(run(*d, d->initial, f + "+2") == run(*d, d->initial, f),
          "Two 45-degree turns equal a quarter turn");
    check(run(*d, d->initial, f + "2") == run(*d, d->initial, f + "_half"),
          "Half-turn primitive agrees with notation");
  }
  check(parse_notation(*d, "U+'").at(0) == "U-", "Prime inverts the complete plus-suffixed token");
  for (const auto &fixture : review.at("legalAlgorithms")) {
    const auto algorithm = fixture.at("notation").get<std::string>();
    const auto target = run(*d, d->initial, algorithm);
    check(run(*d, target, "(" + algorithm + ")'") == d->initial, "Published algorithm has an exact inverse");
  }
  // Konrad's LLL-B1 independently specifies two 3-cycles: three kites and
  // their three attached triangles, with every other piece unchanged.
  const auto cycle = run(*d, d->initial, review.at("pairCycle").at("notation").get<std::string>());
  std::map<std::string, int> moved;
  for (Index i = 0; i < d->pieces.size(); ++i) {
    if (cycle.placement_of[i] == d->initial.placement_of[i])
      continue;
    ++moved[d->pieces[i].type];
    Index next = i;
    std::set<Index> visited;
    do {
      check(visited.insert(next).second, "Pair permutation must close at its starting piece");
      const auto destination = cycle.placement_of[next];
      const auto match = std::find_if(d->pieces.begin(), d->pieces.end(), [&](const Piece &piece) {
        return piece.type == d->pieces[next].type && piece.home == destination;
      });
      check(match != d->pieces.end(), "The pair algorithm returns moved pieces to solved-shaped positions");
      next = static_cast<Index>(match - d->pieces.begin());
    } while (next != i);
    check(visited.size() == 3, "Each published pair permutation is a 3-cycle");
  }
  check(moved["kite-left"] + moved["kite-right"] == 3 &&
            moved["triangle-center"] + moved["triangle-edge"] == 3,
        "Exactly three kite/triangle pairs move");
  int total = 0;
  for (const auto &[_, count] : moved)
    total += count;
  check(total == 6, "The pair cycle leaves all 140 other labeled pieces unchanged");

  const auto prefix = review.at("blocking").at("prefix").get<std::string>();
  const auto operation = review.at("blocking").at("operation").get<std::string>();
  const auto before = session.snapshot();
  const auto failed = session.run(prefix + " " + operation, "transactional", session.revision());
  check(failed.at("status") == "Blocked" && failed.at("reasonCode") == "placement.blocked",
        "A jumbling cut is blocked");
  check(session.snapshot() == before, "Transactional blocking preserves state, revision, and history");
  check(session.run(prefix, "transactional", session.revision()).at("status") == "Committed",
        "The blocking fixture's prefix is legal");
  const auto mixed = session.snapshot();
  check(session.execute(operation, session.revision()).at("status") == "Blocked",
        "Blocking also applies to direct requests");
  check(session.snapshot() == mixed, "A blocked direct request changes nothing");
  check(session.undo(session.revision()).at("status") == "Committed" &&
            session.redo(session.revision()).at("status") == "Committed",
        "Jumbled moves support undo and redo");
  check(session.snapshot().at("stateDigest") == mixed.at("stateDigest"),
        "Redo restores the exact jumbled state");
  Session restored(source);
  check(restored.load(session.save()).at("status") == "Loaded" && restored.state() == session.state(),
        "Replay verifies a jumbled save");

  // Distinct geometric locations can overlap. A shared exclusion resource
  // must reject that assignment even though their location IDs differ.
  bool found = false;
  for (Index i = 0; i < d->pieces.size() && !found; ++i) {
    const auto &domain = d->domains[d->pieces[i].domain];
    for (Index q = 0; q < domain.placements.size() && !found; ++q) {
      const auto &candidate = domain.placements[q];
      for (Index j = 0; j < d->pieces.size() && !found; ++j) {
        if (i == j)
          continue;
        const auto &occupied = d->domains[d->pieces[j].domain].placements[d->pieces[j].home];
        std::vector<std::string> common;
        std::set_intersection(candidate.footprint.begin(), candidate.footprint.end(),
                              occupied.footprint.begin(), occupied.footprint.end(),
                              std::back_inserter(common));
        if (common.empty() || std::any_of(common.begin(), common.end(),
                                          [](const auto &cell) { return cell.starts_with("location/"); }))
          continue;
        auto invalid = d->initial;
        invalid.placement_of[i] = q;
        const auto errors = validate_state(*d, invalid);
        check(std::any_of(errors.begin(), errors.end(),
                          [](const auto &error) { return error.at("reasonCode") == "state.occupancy"; }),
              "Exact overlap exclusions reject distinct-location collisions");
        found = true;
      }
    }
  }
  check(found, "The fixture exercises an actual overlap exclusion");
}
using Point = std::array<double, 3>;
Point sub(Point a, Point b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void geometry_checks(const std::string &source) {
  Session session(source);
  const auto d = session.definition();
  auto realization = Json::parse(read("packages/bagua/bagua-euclidean.json"));
  // Independently check the unshrunk face area, before cosmetic clearances.
  realization["bodyInset"] = 0;
  realization["stickerInset"] = 0;
  realization["stickerLift"] = 0;
  Geometry g(d, realization.dump());
  const auto scene = Json::parse(g.scene_json());
  check(scene.at("catalogTransportVerified") == true,
        "Every catalog transport has a matching geometric track");
  check(scene.at("visualParts").size() == 344, "146 bodies and 198 persistent ports are realized");
  std::map<std::string, Json> assets;
  std::map<std::string, double> areas;
  std::string polygon_model, polygon_port;
  for (const auto &asset : scene.at("meshAssets")) {
    const auto id = asset.at("id").get<std::string>();
    assets[id] = asset;
    const auto offset = asset.at("positionOffset").get<std::size_t>();
    const auto indices = asset.at("indexOffset").get<std::size_t>();
    auto point = [&](Index vertex) {
      return Point{g.positions()[offset + vertex * 3], g.positions()[offset + vertex * 3 + 1],
                   g.positions()[offset + vertex * 3 + 2]};
    };
    for (std::size_t i = 0; i < asset.at("indexCount").get<std::size_t>(); i += 3) {
      const auto a = point(g.indices()[indices + i]), b = point(g.indices()[indices + i + 1]),
                 c = point(g.indices()[indices + i + 2]);
      const auto normal = cross(sub(b, a), sub(c, a));
      check(dot(normal, normal) > 1e-15, "Polygon triangulation emits nondegenerate triangles");
      areas[id] += std::sqrt(dot(normal, normal)) / 2;
    }
  }
  double surface_area = 0;
  for (const auto &part : scene.at("visualParts")) {
    const auto hit = Json::parse(g.bind_hit_json(part.at("visualPartId")));
    check(hit.at("pieceId") == part.at("pieceId"), "Picking preserves piece identity");
    if (part.at("role") == "port") {
      surface_area += areas.at(part.at("meshAssetId"));
      check(hit.at("portId") == part.at("portId"), "Polygon picking preserves its single abstract port");
    }
  }
  check(std::abs(surface_area - 24) < 1e-6,
        "Solved sticker regions cover six complete square faces without adding pieces");
  for (const auto &[model, data] : realization.at("models").items())
    for (const auto &[port, vertices] : data.at("ports").items()) {
      const auto count = vertices.size();
      check(assets.at(model + "/port/" + port).at("indexCount") == 3 * (count - 2),
            "Each polygon is triangulated as one shared port asset");
      if (count > 3) {
        polygon_model = model;
        polygon_port = port;
      }
    }
  check(!polygon_model.empty(), "Bagua exercises polygonal ports");
  auto rejects = [&](Json bad) {
    try {
      Geometry invalid(d, bad.dump());
    } catch (const DiagnosticError &) {
      ++checks;
      return;
    }
    throw std::runtime_error("Malformed port accepted");
  };
  auto bad = realization;
  auto &port = bad["models"][polygon_model]["ports"][polygon_port];
  port[1] = port[0];
  rejects(bad);
  bad = realization;
  auto &model = bad["models"][polygon_model];
  const auto index = model.at("ports").at(polygon_port).at(0).get<Index>();
  for (int axis = 0; axis < 3; ++axis)
    model["vertices"][index][axis] = model["vertices"][index][axis].get<double>() + 0.1;
  rejects(bad);
  for (const auto &op : d->operations) {
    const auto transition = plan(*d, d->initial, op.id);
    check(transition.transition.has_value(), "Every solved-state face turn is legal");
    g.set_state_json(encode_state(*d, d->initial).dump());
    const auto source_frames = g.transforms();
    check(Json::parse(g.prepare_animation_json(encode_transition(*d, *transition.transition).dump()))
                  .at("status") == "Prepared",
          "Prepare each 45/90/180-degree primitive");
    g.sample(0.5);
    const auto middle = g.transforms();
    check(std::all_of(middle.begin(), middle.end(), [](float value) { return std::isfinite(value); }),
          "Sampled frames are finite");
    g.sample(1 - 1e-7);
    const auto near = g.transforms();
    g.sample(1);
    const auto target = g.transforms();
    g.set_state_json(encode_state(*d, transition.transition->after).dump());
    check(target == g.transforms(), "Sampled endpoints equal independent resting frames");
    for (std::size_t i = 0; i < target.size(); ++i)
      check(std::abs(near[i] - target[i]) < 1e-6, "No polygon frame snaps at the endpoint");
    for (std::size_t i = 0; i < scene.at("visualParts").size(); ++i) {
      const auto piece = scene.at("visualParts")[i].at("pieceId").get<std::string>();
      const auto found =
          std::find_if(d->pieces.begin(), d->pieces.end(), [&](const auto &p) { return p.id == piece; });
      const auto pi = static_cast<Index>(found - d->pieces.begin());
      if (transition.transition->before.placement_of[pi] == transition.transition->after.placement_of[pi])
        for (int k = 0; k < 16; ++k)
          check(source_frames[i * 16 + k] == middle[i * 16 + k], "Stationary polygon assets remain fixed");
    }
  }
  Geometry diagram(d, read("packages/bagua/bagua-port-diagram.json"));
  check(Json::parse(diagram.scene_json()).at("visualParts").size() == 198,
        "The diagram reuses exactly the same 198 ports");
}
} // namespace
int main() {
  try {
    const auto source = read("packages/bagua/definition.json");
    model_checks(source);
    geometry_checks(source);
    std::cout << checks << " Bagua checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
