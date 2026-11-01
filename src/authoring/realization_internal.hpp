#pragma once
#include "twisty/execution.hpp"

namespace twisty::language {
// One immutable execution/correspondence export, shared by every view. This is
// an authoring implementation detail; each payload still goes through the
// independent interpreter's validation.
class RealizationExport {
public:
  explicit RealizationExport(std::shared_ptr<const Execution>);
  const Json &abstract() const { return abstract_; }
  const Json &interpretation() const { return interpretation_; }
  const std::shared_ptr<const Execution> &runtime() const { return runtime_; }
  Json view(const std::string &) const;
private:
  std::shared_ptr<const Execution> execution_, runtime_;
  Json abstract_, interpretation_;
};
} // namespace twisty::language
