#pragma once
#include "twisty/compiler.hpp"

namespace twisty {
std::vector<std::string> parse_notation(const Definition &, const std::string &text);

class Session {
public:
  explicit Session(const std::string &source);
  std::shared_ptr<const Definition> definition() const {
    return definition_;
  }
  const State &state() const {
    return state_;
  }
  Json snapshot() const;
  Json execute(const std::string &, const std::string &expected_revision);
  Json run(const std::string &notation, const std::string &policy, const std::string &expected_revision);
  Json undo(const std::string &expected_revision);
  Json redo(const std::string &expected_revision);
  Json scramble(std::uint32_t seed, std::uint32_t length, const std::string &expected_revision);
  Json save() const;
  Json load(const Json &);
  std::string revision() const;
  std::string snapshot_json() const {
    return snapshot().dump();
  }
  std::string definition_json() const;
  std::string state_json() const {
    return encode_state(*definition_, state_).dump();
  }
  std::string execute_json(const std::string &, const std::string &);
  std::string run_json(const std::string &, const std::string &, const std::string &);
  std::string undo_json(const std::string &);
  std::string redo_json(const std::string &);
  std::string scramble_json(std::uint32_t, std::uint32_t, const std::string &);
  std::string save_json() const {
    return save().dump();
  }
  std::string save_with_presentation_json(const std::string &) const;
  std::string load_json(const std::string &);

private:
  std::shared_ptr<const Definition> definition_;
  State origin_;
  State state_;
  std::vector<Transition> history_;
  std::size_t cursor_ = 0;
  std::uint64_t revision_ = 0;
  // Every successful state/history edit advances the revision.
  mutable std::optional<Json> cached_snapshot_;
  mutable std::uint64_t cached_revision_ = 0;
  Json commit(const Transition &, bool truncate = true);
  bool stale(const std::string &) const;
};
} // namespace twisty
