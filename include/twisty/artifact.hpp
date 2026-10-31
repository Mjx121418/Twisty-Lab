#pragma once
#include "twisty/catalog.hpp"
#include "twisty/runtime.hpp"

namespace twisty::artifact {
// Strict, bounded parsing and integer-only canonical bytes for contract 1.
Json parse_document(const std::string &);
std::string canonical_json(const Json &);
std::string document_digest(const Json &, const std::string &own_digest_field);
void seal(Json &, const std::string &own_digest_field);
Json encode_program(const core::ProgramData &);
Json encode_program(const core::ProgramData &, bool grouped_actions);
Json encode(const runtime::CompiledPuzzle &, const std::vector<Index> &request_inverses,
            const std::vector<std::vector<Index>> &choice_inverses);
Json info();
std::string validate_json(const std::string &);

// An immutable, validated source-free artifact. Caller text can be released.
// Bare core programs may omit bindings and session inverse capabilities.
class Artifact {
public:
  explicit Artifact(const std::string &text);
  const Json &document() const { return document_; }
  std::shared_ptr<const runtime::CompiledPuzzle> compiled() const { return compiled_; }
  bool has_session() const { return has_session_; }
  const std::vector<Index> &request_inverses() const { return request_inverses_; }
  const std::vector<std::vector<Index>> &choice_inverses() const { return choice_inverses_; }
private:
  Json document_;
  std::shared_ptr<const runtime::CompiledPuzzle> compiled_;
  bool has_session_ = false;
  std::vector<Index> request_inverses_;
  std::vector<std::vector<Index>> choice_inverses_;
};
} // namespace twisty::artifact
