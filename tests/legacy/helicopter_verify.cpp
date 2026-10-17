#include "twisty/core.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <numeric>
#include <set>
#include <unordered_map>

using namespace twisty;
namespace {
using Shape = std::array<std::uint8_t, 44>;
struct ShapeHash {
  std::size_t operator()(const Shape &shape) const {
    std::size_t hash = 1469598103934665603ULL;
    for (auto value : shape)
      hash = (hash ^ value) * 1099511628211ULL;
    return hash;
  }
};
void check(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
Json read(const std::string &name) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/packages/helicopter/" + name);
  return Json::parse(file);
}
int geometry(const Placement &placement) {
  for (const auto &cell : placement.footprint)
    if (cell.starts_with("location/"))
      return std::stoi(cell.substr(9));
  throw std::runtime_error("Missing abstract shape projection");
}
struct ProjectedOperation {
  const Operation *operation;
  int axis;
  int phase;
  std::array<std::uint8_t, 260> roles{};
  std::array<std::uint8_t, 260> targets{};
};
Shape transformed(const Shape &shape, const Json &action) {
  Shape target{};
  const auto permutation = action.at("permutation").get<std::vector<int>>();
  for (int i = 0; i < 8; ++i)
    target[i] = permutation[shape[i]];
  for (int i = 8; i < 32; ++i)
    target[i] = permutation[80 + shape[i]] - 80;
  for (int axis = 0; axis < 12; ++axis) {
    const auto edge = permutation[224 + axis * 3 + shape[32 + axis]] - 224;
    target[32 + edge / 3] = edge % 3;
  }
  std::sort(target.begin(), target.begin() + 8);
  std::sort(target.begin() + 8, target.begin() + 32);
  return target;
}
} // namespace
int main() {
  try {
    const auto definition = load_definition(read("definition.json"));
    const auto review = read("review.json");
    check(definition->digest == review.at("definitionDigest").get<std::string>(),
          "Review pins different semantics");
    const std::array<std::string, 12> grips = {"UF", "UR", "UB", "UL", "DF", "DR",
                                               "DB", "DL", "FR", "FL", "BR", "BL"};
    std::map<std::string, int> axes;
    for (int i = 0; i < 12; ++i)
      axes[grips[i]] = i;
    // Expand abstract capacity-one resources into bit masks for an exhaustive
    // occupancy check. Corner sticker orientations share the same resources.
    std::map<std::string, std::set<int>> resource_locations;
    for (const auto &domain : definition->domains)
      for (const auto &placement : domain.placements)
        for (const auto &cell : placement.footprint)
          resource_locations[cell].insert(geometry(placement));
    using Mask = std::array<std::uint64_t, 5>;
    std::array<Mask, 260> incompatible{};
    for (const auto &[_, locations] : resource_locations)
      for (const auto from : locations)
        for (const auto other : locations)
          incompatible[from][other / 64] |= std::uint64_t{1} << (other % 64);
    const auto valid_shape = [&](const Shape &shape) {
      Mask occupied{};
      for (int i = 0; i < 44; ++i) {
        const int location = i < 8 ? shape[i] : i < 32 ? 80 + shape[i] : 224 + (i - 32) * 3 + shape[i];
        for (int word = 0; word < 5; ++word)
          if (occupied[word] & incompatible[location][word])
            return false;
        occupied[location / 64] |= std::uint64_t{1} << (location % 64);
      }
      return true;
    };
    std::vector<ProjectedOperation> operations;
    for (const auto &operation : definition->operations) {
      ProjectedOperation projected{&operation, axes.at(operation.family), operation.id[3] - 'a'};
      std::array<bool, 260> assigned{};
      for (Index domain = 0; domain < definition->domains.size(); ++domain) {
        const auto &placements = definition->domains[domain].placements;
        for (Index q = 0; q < placements.size(); ++q) {
          const auto from = geometry(placements[q]);
          const auto to = geometry(placements[operation.maps[domain][q]]);
          const auto role = operation.roles[domain][q];
          const auto target = to < 80 ? to : to < 224 ? to - 80 : (to - 224) % 3;
          if (assigned[from]) {
            check(projected.roles[from] == role && projected.targets[from] == target,
                  "Shape transition depends on a corner's sticker orientation");
          }
          assigned[from] = true;
          projected.roles[from] = role;
          projected.targets[from] = target;
        }
      }
      check(std::all_of(assigned.begin(), assigned.end(), [](bool x) { return x; }),
            "Incomplete shape projection");
      check(operation.piece_guards.size() == 1, "Expected one grip phase guard");
      const auto [piece, placement] = *operation.piece_guards.begin();
      check(definition->pieces[piece].id == "mechanism/" + operation.family &&
                geometry(definition->domains[definition->pieces[piece].domain].placements[placement]) ==
                    224 + projected.axis * 3 + projected.phase,
            "Projected guard differs from the core's guard");
      operations.push_back(projected);
    }
    Shape origin{};
    int c = 0, f = 8;
    for (Index piece = 0; piece < definition->pieces.size(); ++piece) {
      const auto &p = definition->pieces[piece];
      const auto g =
          geometry(definition->domains[p.domain].placements[definition->initial.placement_of[piece]]);
      if (p.type == "corner")
        origin[c++] = g;
      else if (p.type == "center")
        origin[f++] = g - 80;
      else
        origin[32 + axes.at(p.id.substr(10))] = (g - 224) % 3;
    }
    check(c == 8 && f == 32, "Wrong visible piece counts");
    std::sort(origin.begin(), origin.begin() + 8);
    std::sort(origin.begin() + 8, origin.begin() + 32);
    check(valid_shape(origin), "Initial shape violates placement exclusions");
    std::unordered_map<Shape, std::uint8_t, ShapeHash> depths;
    depths.reserve(700000);
    depths.emplace(origin, 0);
    std::vector<Shape> queue{origin};
    queue.reserve(700000);
    std::vector<std::size_t> histogram(29);
    histogram[0] = 1;
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
      const auto current = queue[cursor];
      const int depth = depths.at(current);
      for (const auto &operation : operations) {
        if (current[32 + operation.axis] != operation.phase)
          continue;
        Shape next = current;
        bool blocked = false;
        for (int i = 0; i < 44; ++i) {
          const int from = i < 8 ? current[i] : i < 32 ? current[i] + 80 : 224 + (i - 32) * 3 + current[i];
          if (operation.roles[from] == 2) {
            blocked = true;
            break;
          }
          if (operation.roles[from] == 1)
            next[i] = operation.targets[from];
        }
        if (blocked)
          continue;
        std::sort(next.begin(), next.begin() + 8);
        std::sort(next.begin() + 8, next.begin() + 32);
        if (depths.contains(next))
          continue;
        check(valid_shape(next), "A reachable shape violates placement exclusions");
        check(depth < 28 && queue.size() < 700000, "Shape graph exceeds the verified scope or memory cap");
        depths.emplace(next, depth + 1);
        queue.push_back(next);
        ++histogram[depth + 1];
      }
    }
    check(queue.size() == review.at("expected").at("orientedShapes"), "Oriented shape count differs");
    check(histogram[28] > 0, "Maximum shape depth differs");
    const auto &actions = review.at("shapeSymmetries");
    check(actions.size() == 48, "Expected 48 shape symmetries");
    for (const bool mirrors : {false, true}) {
      const std::uint8_t flag = mirrors ? 128 : 64;
      std::vector<std::size_t> classes(29);
      for (const auto &shape : queue) {
        auto &record = depths.at(shape);
        if (record & flag)
          continue;
        const auto depth = record & 31;
        ++classes[depth];
        for (const auto &action : actions) {
          if (!mirrors && action.at("orientation") != 1)
            continue;
          const auto target = transformed(shape, action);
          const auto found = depths.find(target);
          check(found != depths.end() && (found->second & 31) == depth,
                "Symmetry failed to preserve reachability and depth");
          found->second |= flag;
        }
      }
      check(classes == review.at("expected")
                           .at(mirrors ? "mirrorClassesByDepth" : "rotationClassesByDepth")
                           .get<std::vector<std::size_t>>(),
            "Published depth distribution differs");
    }
    std::cout << Json{{"status", "Passed"},
                      {"definitionDigest", definition->digest},
                      {"orientedShapes", queue.size()},
                      {"rotationClasses", 28055},
                      {"mirrorClasses", 14098},
                      {"maximumDepth", 28},
                      {"orientedShapesByDepth", histogram}}
                     .dump()
              << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
