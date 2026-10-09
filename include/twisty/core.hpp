#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace twisty {
using Json = nlohmann::json;
using Index = std::uint32_t;
std::string sha256(const std::string &value);

struct DiagnosticError : std::runtime_error {
  std::string code;
  std::string source;
  DiagnosticError(std::string code, std::string source, std::string message);
  Json json() const;
};

struct Placement {
  std::string key;
  std::vector<std::string> footprint;
  // Definition-local indices accelerate occupancy without changing canonical data.
  std::vector<Index> footprint_cells;
  // Each port names an abstract cell and attachment, never a coordinate.
  std::map<std::string, std::pair<std::string, std::string>> ports;
};
struct Domain {
  std::string id;
  std::vector<Placement> placements;
  std::map<std::string, Index> by_key;
};
struct Piece {
  std::string id;
  std::string type;
  Index domain;
  Index home;
  std::map<std::string, std::string> labels;
};
struct Operation {
  std::string id;
  std::string inverse;
  std::string family;
  std::string transport;
  std::vector<std::string> cells;
  std::vector<std::vector<Index>> maps;
  // Placement relations: 0 = stationary, 1 = participant, 2 = blocked.
  std::vector<std::vector<std::uint8_t>> roles;
  std::map<Index, Index> piece_guards;
  Json mechanism_guard = Json::object();
  Json mechanism_update = Json::object();
};
struct State {
  std::vector<Index> placement_of;
  Json mechanism = Json::object();
  bool operator==(const State &) const = default;
};
struct Definition {
  std::string id;
  std::string digest;
  Json canonical;
  Json provenance;
  std::vector<std::string> cells;
  std::vector<Domain> domains;
  std::vector<Piece> pieces;
  std::vector<Operation> operations;
  std::map<std::string, Index> operation_ids;
  State initial;
  bool placement_relations = false;
};
struct Action {
  Index piece;
  Index from;
  Index to;
};
struct Transition {
  std::string operation;
  State before;
  State after;
  std::vector<Action> actions;
};
struct Plan {
  std::optional<Transition> transition;
  Json blocked;
};

std::shared_ptr<const Definition> load_definition(Json compiled);
Json validate_state(const Definition &, const State &);
Json encode_state(const Definition &, const State &);
State decode_state(const Definition &, const Json &);
std::string state_digest(const Definition &, const State &);
bool solved(const Definition &, const State &);
Plan plan(const Definition &, const State &, const std::string &operation);
Json encode_transition(const Definition &, const Transition &);
Json legal_operations(const Definition &, const State &);
Json diagnostic_result(const std::exception &);
void require_exact(const Json &, const std::string &path);
} // namespace twisty
