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
// Symbolic interpreter/inspector catalog. The abstract executor lives in core.hpp.

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
  // Interpreter transport certificate: 0 = stationary, 1 = participant, 2 = unavailable.
  std::vector<std::vector<std::uint8_t>> roles;
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
  std::vector<std::string> cells;
  std::vector<Domain> domains;
  std::vector<Piece> pieces;
  std::vector<Operation> operations;
  std::map<std::string, Index> operation_ids;
  State initial;
  // Interpreter certificate kind only; never participates in abstract rules.
  bool port_catalog = false;
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

Json encode_state(const Definition &, const State &);
std::string state_digest(const Definition &, const State &);
Json encode_transition(const Definition &, const Transition &);
Json diagnostic_result(const std::exception &);
} // namespace twisty
