#pragma once
#include "twisty/artifact.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace twisty::artifact::detail {
Json parse_geometry_document(const std::string &);
inline void require(bool ok, const std::string &path, const std::string &message,
                    const std::string &code = "artifact.invalid") {
  if (!ok) throw DiagnosticError(code, path, message);
}
inline void fields(const Json &j, std::initializer_list<const char *> required,
                   std::initializer_list<const char *> optional = {}) {
  require(j.is_object(), "/", "Expected a contract object.");
  for (auto key : required) require(j.contains(key), "/" + std::string(key), "Missing required field.");
  for (const auto &[key, value] : j.items()) {
    (void)value;
    const auto equal = [&](auto k) { return key == k; };
    require(std::any_of(required.begin(), required.end(), equal) ||
            std::any_of(optional.begin(), optional.end(), equal), "/" + key, "Unknown contract field.");
  }
}
inline Index index(const Json &v) {
  require(v.is_number(), "/", "Expected an unsigned integer.");
  const auto n = v.get<double>();
  require(std::isfinite(n) && n >= 0 && n <= 4294967294.0 && std::floor(n) == n,
          "/", "Expected an exact uint32 excluding the reserved sentinel.");
  return static_cast<Index>(n);
}
inline const Json &array(const Json &v, std::size_t limit = 1000000) {
  require(v.is_array() && v.size() <= limit, "/", "Array exceeds its contract limit.", "artifact.limit");
  return v;
}
inline std::vector<Index> indices(const Json &v, std::size_t limit = 4000000) {
  std::vector<Index> out;
  for (const auto &x : array(v, limit)) out.push_back(index(x));
  return out;
}
inline std::string id(const Json &v) {
  require(v.is_string(), "/", "Expected a stable ID.");
  auto s = v.get<std::string>();
  require(!s.empty() && s.size() <= 256 && s.find('\0') == std::string::npos, "/", "Invalid ID length or NUL.");
  return s;
}
inline std::vector<std::string> ids(const Json &v, std::size_t limit = 1000000) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (const auto &x : array(v, limit)) {
    auto s = id(x);
    require(seen.insert(s).second, "/", "Duplicate stable ID.");
    out.push_back(std::move(s));
  }
  return out;
}
inline void pin(const Json &doc, const std::string &key) {
  require(doc.contains(key) && doc.at(key).is_string() &&
          doc.at(key).get_ref<const std::string &>().size() == 64 &&
          doc.at(key) == document_digest(doc, key), "/" + key,
          "Contract digest differs from canonical contents.", "artifact.digest_mismatch");
}
} // namespace twisty::artifact::detail
