#include "twisty/controller.hpp"
#include "twisty/session.hpp"
#include <set>

namespace twisty {
namespace {
template <class F> std::string guarded(F f) {
  try {
    return f().dump();
  } catch (const std::exception &e) {
    return diagnostic_result(e).dump();
  }
}
Json stale(const Session &s) {
  return {{"status", "StaleRevision"}, {"revision", s.revision()}};
}
Json stamp(Json j, const Session &s) {
  j["revision"] = s.revision();
  j["definitionDigest"] = s.definition()->digest;
  return j;
}
} // namespace
Controller::Controller(std::shared_ptr<const Execution> e) : execution_(std::move(e)) {
  if (!execution_)
    throw DiagnosticError("controls.execution", "/", "An execution context is required.");
}
Json Controller::catalog() const {
  auto c = execution_->controls();
  Json result{{"version", 1}, {"drivers", Json::array()}, {"families", Json::array()}};
  if (!c)
    return result;
  result["drivers"] = c->json().at("drivers");
  for (const auto &f : c->json().at("families"))
    result["families"].push_back({{"id", f.at("id")},
                                  {"label", f.value("label", f.at("id").get<std::string>())},
                                  {"drivers", f.at("drivers")}});
  return result;
}
Json Controller::family_state(const State &s, const std::string &id) const {
  execution_->validate(s);
  return read_family_state(s, id);
}
Json Controller::read_family_state(const State &s, const std::string &id) const {
  const auto c = execution_->controls();
  if (!c)
    return {{"status", "Unavailable"}, {"reasonCode", "controls.absent"}};
  const auto &f = c->family(id);
  const auto stops = c->read(s, f);
  if (!stops)
    return {{"status", "Unavailable"}, {"reasonCode", "controls.phase_unavailable"}, {"family", id}};
  Json current = Json::object();
  for (Index i = 0; i < f.drivers.size(); ++i)
    current[c->driver(f.drivers[i]).id] = stops->at(i);
  return {{"status", "Ready"}, {"family", id}, {"current", current}};
}
Json Controller::plan_to(const State &s, const Json &intent) const {
  execution_->validate(s);
  return resolve(s, intent);
}
Json Controller::resolve(const State &s, const Json &intent) const {
  const auto id = intent.at("family").get<std::string>();
  auto result = read_family_state(s, id);
  if (result.at("status") != "Ready")
    return result;
  const auto c = execution_->controls();
  const auto &f = c->family(id);
  const auto from = *c->read(s, f);
  const auto &targets = intent.at("targets");
  if (!targets.is_object() || targets.size() != f.drivers.size())
    throw DiagnosticError("controls.targets", "/targets", "Supply exactly the family's driver targets.");
  std::vector<std::string> to;
  for (auto d : f.drivers) {
    const auto &driver = c->driver(d);
    const auto stop = targets.at(driver.id).get<std::string>();
    if (std::find(driver.stops.begin(), driver.stops.end(), stop) == driver.stops.end())
      throw DiagnosticError("controls.stop", "/targets", "Unknown destination stop.");
    to.push_back(stop);
  }
  const auto path = intent.value("pathId", std::string{});
  if (to == from && path.empty())
    return {{"status", "NoOp"}, {"family", id}};
  std::vector<std::pair<const ControlCatalog::Route *, Json>> valid;
  Json blocked;
  bool matched_path = path.empty();
  const auto routes = f.routes.find(from);
  if (routes != f.routes.end())
    for (const auto &r : routes->second) {
      if (!path.empty() && r.path != path)
        continue;
      matched_path = true;
      if (r.to != to)
        continue;
      const auto plan = execution_->plan(s, r.request, *r.choices);
      if (!plan.transition) {
        if (blocked.is_null())
          blocked = plan.blocked;
        continue;
      }
      if (c->read(plan.transition->after, f) != std::optional(to))
        throw DiagnosticError("controls.route_target", "/catalog/controls",
                              "Route does not reach its declared target stops.");
      auto wire = execution_->encode(*plan.transition);
      valid.emplace_back(&r, std::move(wire));
    }
  if (!matched_path)
    throw DiagnosticError("controls.path", "/pathId", "Unknown path at this source phase.");
  if (valid.empty()) {
    if (blocked.is_null())
      blocked = {{"status", "NoDirectRoute"}, {"reasonCode", "controls.no_route"}};
    blocked["family"] = id;
    return blocked;
  }
  std::size_t chosen = 0, preferred = 0;
  for (std::size_t i = 0; i < valid.size(); ++i)
    if (valid[i].first->preferred) {
      chosen = i;
      ++preferred;
    }
  if (valid.size() > 1 && preferred != 1) {
    Json paths = Json::array();
    for (const auto &[r, w] : valid)
      paths.push_back({{"pathId", r->path}, {"request", w.at("request")}});
    return {{"status", "ChoiceRequired"}, {"paths", paths}, {"family", id}};
  }
  auto &[route, transition] = valid[chosen];
  return {{"status", "Planned"},      {"family", id},
          {"pathId", route->path},    {"request", transition.at("request")},
          {"transition", transition}, {"transitionRecord", transition.dump()}};
}
void Controller::same_session(const Session &s) const {
  if (s.execution() != execution_)
    throw DiagnosticError("controls.session", "/", "Controller belongs to a different execution context.");
}
Json Controller::family_state(const Session &s, const std::string &f) const {
  same_session(s);
  return stamp(read_family_state(s.state(), f), s);
}
Json Controller::plan_to(const Session &s, const Json &intent, const std::string &expected) const {
  same_session(s);
  if (s.revision() != expected)
    return stale(s);
  return stamp(resolve(s.state(), intent), s);
}
Json Controller::turn_to(Session &s, const Json &intent, const std::string &expected) const {
  auto plan = plan_to(s, intent, expected);
  if (plan.at("status") != "Planned")
    return plan;
  const auto &r = plan.at("request");
  auto result = s.execute(r.at("operation"), r.at("choices").get<std::vector<std::string>>(), expected,
                          SnapshotDetail::StateOnly);
  result["family"] = plan.at("family");
  result["pathId"] = plan.at("pathId");
  return result;
}
Json Controller::destinations(const Session &s, const std::string &id, Index offset, Index limit) const {
  same_session(s);
  auto result = family_state(s, id);
  if (result.at("status") != "Ready")
    return result;
  if (limit == 0 || limit > 32)
    throw DiagnosticError("controls.limit", "/limit", "Preview pages contain 1–32 destinations.");
  const auto c = execution_->controls();
  const auto &f = c->family(id);
  std::size_t total = 1;
  for (auto d : f.drivers)
    total *= c->driver(d).stops.size();
  result["destinations"] = Json::array();
  result["total"] = total;
  for (std::size_t n = offset; n < total && n < std::size_t(offset) + limit; ++n) {
    auto k = n;
    Json targets = Json::object();
    for (auto it = f.drivers.rbegin(); it != f.drivers.rend(); ++it) {
      const auto &d = c->driver(*it);
      targets[d.id] = d.stops[k % d.stops.size()];
      k /= d.stops.size();
    }
    auto preview = resolve(s.state(), {{"family", id}, {"targets", targets}});
    preview["targets"] = targets;
    // A preview is descriptive; do not retain a page of full state witnesses.
    preview.erase("transition");
    preview.erase("transitionRecord");
    result["destinations"].push_back(std::move(preview));
  }
  result["nextOffset"] = std::min<std::size_t>(total, std::size_t(offset) + limit);
  return result;
}
std::string Controller::catalog_json() const {
  return catalog().dump();
}
std::string Controller::family_state_json(const Session &s, const std::string &f) const {
  return guarded([&] { return family_state(s, f); });
}
std::string Controller::destinations_json(const Session &s, const std::string &f, Index o, Index n) const {
  return guarded([&] { return destinations(s, f, o, n); });
}
std::string Controller::plan_to_json(const Session &s, const std::string &i, const std::string &r) const {
  return guarded([&] { return plan_to(s, Json::parse(i), r); });
}
std::string Controller::turn_to_json(Session &s, const std::string &i, const std::string &r) const {
  return guarded([&] {
    auto result = turn_to(s, Json::parse(i), r);
    if (result.contains("transitions")) {
      result["transitionRecords"] = Json::array();
      for (const auto &t : result.at("transitions"))
        result["transitionRecords"].push_back(t.dump());
    }
    return result;
  });
}
} // namespace twisty
