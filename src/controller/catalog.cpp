#include "twisty/controller.hpp"
#include <set>

namespace twisty {
namespace {
void require(bool ok, const std::string &message) {
  if (!ok)
    throw DiagnosticError("controls.invalid", "/catalog/controls", message);
}
std::string symbol(const Json &value) {
  require(value.is_string() && !value.get_ref<const std::string &>().empty(), "Expected a nonempty ID.");
  return value.get<std::string>();
}
} // namespace
std::shared_ptr<const ControlCatalog> ControlCatalog::compile(const Definition &definition,
                                                              const Json &source, const Json &wire) {
  auto result = std::make_shared<ControlCatalog>();
  result->wire_ = wire;
  require(wire.is_object() && wire.at("version") == 1, "Unsupported control catalog version.");
  require(wire.at("drivers").is_array() && wire.at("drivers").size() <= 128 &&
              wire.at("families").is_array() && wire.at("families").size() <= 256,
          "Control catalog exceeds driver/family bounds.");
  std::map<std::string, Index> drivers, pieces;
  for (Index p = 0; p < definition.pieces.size(); ++p)
    pieces.emplace(definition.pieces[p].id, p);
  for (const auto &entry : wire.at("drivers")) {
    Driver d;
    d.id = symbol(entry.at("id"));
    require(drivers.emplace(d.id, result->drivers_.size()).second, "Duplicate driver ID.");
    const auto piece = symbol(entry.at("piece"));
    require(pieces.contains(piece), "Unknown phase-reader piece.");
    d.piece = pieces.at(piece);
    require(entry.at("stops").is_array() && entry.at("stops").size() >= 2 && entry.at("stops").size() <= 128,
            "A driver needs 2–128 stops.");
    std::set<std::string> stops;
    for (const auto &s : entry.at("stops")) {
      auto name = symbol(s);
      require(stops.insert(name).second, "Duplicate stop ID.");
      d.stops.push_back(name);
    }
    const auto &domain = definition.domains.at(definition.pieces[d.piece].domain);
    require(entry.at("placements").is_object() && !entry.at("placements").empty(), "Empty phase reader.");
    std::set<std::string> covered;
    for (const auto &[q, stop] : entry.at("placements").items()) {
      require(domain.by_key.contains(q) && stops.contains(symbol(stop)),
              "Invalid phase-reader placement/stop.");
      d.reader.emplace(domain.by_key.at(q), stop.get<std::string>());
      covered.insert(stop.get<std::string>());
    }
    require(covered == stops, "Every declared stop needs a phase-reader placement.");
    result->drivers_.push_back(std::move(d));
  }
  std::map<std::string, const Json *> requests;
  for (const auto &r : source.at("requests"))
    requests.emplace(r.at("id"), &r);
  std::size_t route_count = 0;
  std::map<std::pair<std::string, std::string>, std::shared_ptr<const std::vector<std::string>>> completions;
  for (const auto &entry : wire.at("families")) {
    Family f;
    f.id = symbol(entry.at("id"));
    require(!result->families_.contains(f.id), "Duplicate control family ID.");
    require(entry.at("drivers").is_array() && !entry.at("drivers").empty() && entry.at("drivers").size() <= 2,
            "A family needs one or two drivers.");
    std::set<Index> used;
    for (const auto &id : entry.at("drivers")) {
      require(drivers.contains(symbol(id)), "Unknown family driver.");
      auto index = drivers.at(id.get<std::string>());
      require(used.insert(index).second, "Duplicate family driver.");
      f.drivers.push_back(index);
    }
    require(entry.at("routes").is_array(), "Expected finite routes.");
    route_count += entry.at("routes").size();
    require(route_count <= 131072, "Control route budget exceeded.");
    std::set<std::pair<std::vector<std::string>, std::string>> unique;
    for (const auto &r : entry.at("routes")) {
      Route route;
      route.from = r.at("from").get<std::vector<std::string>>();
      route.to = r.at("to").get<std::vector<std::string>>();
      require(route.from.size() == f.drivers.size() && route.to.size() == f.drivers.size(),
              "Route arity differs.");
      for (Index i = 0; i < f.drivers.size(); ++i) {
        const auto &stops = result->driver(f.drivers[i]).stops;
        require(std::find(stops.begin(), stops.end(), route.from[i]) != stops.end() &&
                    std::find(stops.begin(), stops.end(), route.to[i]) != stops.end(),
                "Unknown route stop.");
      }
      route.request = symbol(r.at("request"));
      require(requests.contains(route.request), "Unknown route request.");
      route.path = r.value("pathId", route.request);
      require(!route.path.empty() && unique.emplace(route.from, route.path).second,
              "Duplicate source/path route.");
      route.preferred = r.value("preferred", false);
      const auto &request = *requests.at(route.request);
      const auto profile = r.value("profile", request.at("defaultProfile").get<std::string>());
      const Json *completion = nullptr;
      for (const auto &p : request.at("profiles"))
        if (p.at("id") == profile)
          completion = &p;
      require(completion, "Unknown route completion profile.");
      auto &tuple = completions[{route.request, profile}];
      if (!tuple) {
        auto choices = std::make_shared<std::vector<std::string>>();
        for (const auto &domain : source.at("domains"))
          choices->push_back(
              completion->at("choices").at(domain.at("id").get<std::string>()).get<std::string>());
        tuple = std::move(choices);
      }
      route.choices = tuple;
      f.routes[route.from].push_back(std::move(route));
    }
    for (const auto &[from, routes] : f.routes) {
      for (const auto &route : routes) {
        const auto inverse = definition.operations.at(definition.operation_ids.at(route.request)).inverse;
        const auto reverse = f.routes.find(route.to);
        require(reverse != f.routes.end() &&
                    std::any_of(reverse->second.begin(), reverse->second.end(),
                                [&](const Route &r) { return r.to == from && r.request == inverse; }),
                "Each route needs a corresponding inverse route.");
      }
    }
    result->families_.emplace(f.id, std::move(f));
  }
  return result;
}
const ControlCatalog::Family &ControlCatalog::family(const std::string &id) const {
  const auto it = families_.find(id);
  if (it == families_.end())
    throw DiagnosticError("controls.family", "/family", "Unknown control family.");
  return it->second;
}
std::optional<std::vector<std::string>> ControlCatalog::read(const State &state, const Family &f) const {
  std::vector<std::string> stops;
  for (auto i : f.drivers) {
    const auto &d = driver(i);
    const auto it = d.reader.find(state.placement_of.at(d.piece));
    if (it == d.reader.end())
      return std::nullopt;
    stops.push_back(it->second);
  }
  return stops;
}
} // namespace twisty
