#pragma once
#include <cstddef>
#include <string_view>

namespace twisty::detail {
// Bound nesting before allocating a DOM. A JSON parse callback is unsuitable
// for this check: its discard handling repeatedly scans broad parent objects.
// Syntax validation remains the responsibility of the normal JSON parser.
template <class Failure> void check_json_depth(std::string_view text, std::size_t limit, Failure failure) {
  std::size_t depth = 0;
  bool quoted = false, escaped = false;
  for (const char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"') {
      if (depth > limit)
        failure();
      quoted = true;
    } else if (c == '{' || c == '[') {
      // Match the previous callback's event depth: an empty container may
      // start at the limit; keys and values inside it count one level deeper.
      if (depth > limit)
        failure();
      ++depth;
    } else if ((c == '}' || c == ']') && depth > 0)
      --depth;
    else if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != ':' && c != ',' && depth > limit)
      failure();
  }
}
} // namespace twisty::detail
