#pragma once
#include "twisty/execution.hpp"

namespace twisty {
enum class SnapshotDetail { Full, StateOnly };
std::vector<std::string> parse_notation(const Definition &, const std::string &text);

class Session {
public:
  explicit Session(const std::string &source);
  // A fresh logical session sharing immutable compiled data; no history is copied.
  explicit Session(std::shared_ptr<const Execution>);
  // Interpreter/inspector metadata. Use execution() for logical queries.
  std::shared_ptr<const Definition> definition() const {
    return definition_;
  }
  std::shared_ptr<const Execution> execution() const {
    return execution_;
  }
  const State &state() const {
    return state_;
  }
  Json snapshot(SnapshotDetail detail = SnapshotDetail::Full) const;
  Json execute(const std::string &, const std::string &expected_revision);
  Json execute(const std::string &operation, const std::vector<std::string> &choices,
               const std::string &expected_revision, SnapshotDetail detail = SnapshotDetail::Full);
  // Presentation configuration only; state/history and full snapshot() are unchanged.
  void lightweight_results(bool value) { result_detail_ = value ? SnapshotDetail::StateOnly : SnapshotDetail::Full; }
  std::string state_snapshot_json() const { return snapshot(SnapshotDetail::StateOnly).dump(); }
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
  std::string plan_json(const std::string &request, const std::string &state) const;
  std::string validate_state_json(const std::string &state) const;
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
  std::shared_ptr<const Execution> execution_;
  std::shared_ptr<const Definition> definition_;
  State origin_;
  State state_;
  std::vector<CompletedTransition> history_;
  std::size_t cursor_ = 0;
  std::uint64_t revision_ = 0;
  // Every successful state/history edit advances the revision.
  mutable std::optional<Json> cached_snapshot_;
  mutable std::uint64_t cached_revision_ = 0;
  SnapshotDetail result_detail_ = SnapshotDetail::Full;
  Json commit(const CompletedTransition &, bool truncate = true);
  Json execute_completed(const std::string &, const Json &choices, const std::string &expected,
                         SnapshotDetail detail);
  bool stale(const std::string &) const;
};
} // namespace twisty
