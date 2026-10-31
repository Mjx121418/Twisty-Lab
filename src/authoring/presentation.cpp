#include "twisty/contracts.hpp"
#include "presentation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace twisty::language {
Json movement_presentation(const Json &definition, const Json &controls, const Json &views) {
  const auto empty = Json::object();
  const auto &geometry = views.empty() ? empty : views.front();
  const auto &layout = geometry.contains("controlLayout") ? geometry.at("controlLayout") : empty;
  const bool relative = controls.at("families").empty() && geometry.contains("rotationTracks") &&
    geometry.contains("motionFamilies");
  const std::string interaction = !controls.at("families").empty() ? "destinations" :
    layout.value("version", 0) == 2 ? "cell-axes" : relative ? "relative" : "moves";
  Json display{{"version",contracts::movement_presentation_version}, {"operations",Json::object()}, {"familyOrder",Json::array()},
    {"familyProbes",Json::object()}, {"keyboard",Json::object()}, {"interaction",interaction}};
  std::set<std::string> families;
  for (const auto &op : definition.at("operations")) {
    const auto id = op.at("id").get<std::string>(), family = op.at("family").get<std::string>();
    families.insert(family);
    if (!display["familyProbes"].contains(family) || id == family) display["familyProbes"][family] = id;
    const bool inverse = id == family + "'" || id == family + "_inv", half = id == family + "_half";
    Json entry{{"label",inverse ? "′" : half ? family + "2" : id},
      {"accessibleLabel",inverse ? family + " inverse" : half ? family + " half turn" : id}, {"aliases",Json::array()}};
    Json angles = Json::array();
    // Naming conventions are lowered once at the authoring boundary. Runtime
    // consumers only use explicit request references and display values.
    if (layout.value("version",0) == 1 && layout.contains("paths")) {
      const auto append = [&](const std::string &path_id) {
        if (layout.at("paths").contains(path_id)) {
          const auto &path = layout.at("paths").at(path_id);
          if (!path.empty()) angles.push_back(path.begin().value().get<double>() * 180 / std::numbers::pi);
        }
      };
      if (layout.at("paths").contains(id)) append(id);
      else {
        std::size_t begin = 0;
        do {
          const auto end = id.find('+',begin);
          const auto part = id.substr(begin,end-begin);
          append(part);
          if (end == std::string::npos) break;
          begin = end + 1;
        } while (begin < id.size());
      }
    }
    if (geometry.contains("rotationTracks") && geometry.at("rotationTracks").contains(id)) {
      const auto &track = geometry.at("rotationTracks").at(id);
      if (track.is_object() && track.contains("family") && geometry.contains("motionFamilies")) {
        const auto &coordinates = geometry.at("motionFamilies").at("families").at(track.at("family").get<std::string>()).at("coordinates");
        angles = Json::array({-(coordinates.at(track.at("to").get<std::string>()).get<double>() -
          coordinates.at(track.at("from").get<std::string>()).get<double>() + track.at("winding").get<int>() * 2 * std::numbers::pi) * 180 / std::numbers::pi});
      }
    }
    if (!angles.empty()) entry["angleDegrees"] = std::move(angles);
    display["operations"][id] = std::move(entry);
    if (id == family || id == family + "q") display["keyboard"][family].push_back(id);
  }
  const std::vector<std::string> cube_order{"U","R","F","D","L","B"};
  if (std::all_of(cube_order.begin(), cube_order.end(), [&](const auto &face) { return families.contains(face); }))
    for (const auto &face : cube_order) { families.erase(face); display["familyOrder"].push_back(face); }
  for (const auto &family : families) display["familyOrder"].push_back(family);
  if (!definition.at("operations").empty()) {
    const auto &first = definition.at("operations").front();
    display["defaultAlgorithm"] = first.at("id").get<std::string>() + " " + first.at("inverse").get<std::string>();
  }
  display["instructions"] = interaction == "destinations" ? "Choose a twist family and destination. Select pieces to find available families." :
    interaction == "relative" ? "Choose an axis and angular destination. Select pieces to find available axes." :
    interaction == "cell-axes" ? "Choose a cell, axis and turn angle." : "Choose a movement. Shift reverses a face key.";
  return display;
}
} // namespace twisty::language
