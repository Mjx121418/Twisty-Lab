#pragma once
#include "twisty/execution.hpp"

namespace twisty {
class Session;

// Immutable authored controls, independent of transport compression and geometry.
class ControlCatalog {
public:
  struct Driver {
    std::string id;
    Index piece;
    std::vector<std::string> stops;
    std::map<Index, std::string> reader;
  };
  struct Route {
    std::vector<std::string> from, to;
    std::shared_ptr<const std::vector<std::string>> choices;
    std::string request, path;
    bool preferred = false;
  };
  struct Family {
    std::string id;
    std::vector<Index> drivers;
    std::map<std::vector<std::string>, std::vector<Route>> routes;
  };
  static std::shared_ptr<const ControlCatalog> compile(const Definition &, const Json &source,
                                                       const Json &controls);
  const Json &json() const {
    return wire_;
  }
  const Family &family(const std::string &) const;
  std::optional<std::vector<std::string>> read(const State &, const Family &) const;
  const Driver &driver(Index i) const {
    return drivers_.at(i);
  }

private:
  Json wire_;
  std::vector<Driver> drivers_;
  std::map<std::string, Family> families_;
};

// Pure queries share an Execution; mutations require an explicit live Session.
class Controller {
public:
  explicit Controller(std::shared_ptr<const Execution>);
  Json catalog() const;
  Json family_state(const State &, const std::string &family) const;
  Json plan_to(const State &, const Json &intent) const;
  Json family_state(const Session &, const std::string &family) const;
  Json destinations(const Session &, const std::string &family, Index offset = 0, Index limit = 16) const;
  Json plan_to(const Session &, const Json &intent, const std::string &expected) const;
  Json turn_to(Session &, const Json &intent, const std::string &expected) const;
  std::string catalog_json() const;
  std::string family_state_json(const Session &, const std::string &) const;
  std::string destinations_json(const Session &, const std::string &, Index, Index) const;
  std::string plan_to_json(const Session &, const std::string &, const std::string &) const;
  std::string turn_to_json(Session &, const std::string &, const std::string &) const;

private:
  std::shared_ptr<const Execution> execution_;
  Json read_family_state(const State &, const std::string &) const;
  Json resolve(const State &, const Json &) const;
  void same_session(const Session &) const;
};
} // namespace twisty
