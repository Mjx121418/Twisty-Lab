#include "twisty/session.hpp"
#include <algorithm>
#include <cctype>

namespace twisty {
namespace {
class Parser {
public:
  Parser(const Definition &definition, const std::string &text) : definition_(definition), text_(text) {}
  std::vector<std::string> parse() {
    auto result = sequence("");
    skip();
    if (at_ != text_.size())
      fail("Unexpected trailing notation.");
    return result;
  }

private:
  const Definition &definition_;
  const std::string &text_;
  std::size_t at_ = 0;
  int depth_ = 0;
  [[noreturn]] void fail(const std::string &message) const {
    throw DiagnosticError("notation.invalid", "/notation/" + std::to_string(at_), message);
  }
  void skip() {
    while (at_ < text_.size()) {
      if (std::isspace(static_cast<unsigned char>(text_[at_]))) {
        ++at_;
        continue;
      }
      if (text_[at_] == '#' || text_.compare(at_, 2, "//") == 0) {
        while (at_ < text_.size() && text_[at_] != '\n')
          ++at_;
        continue;
      }
      break;
    }
  }
  std::vector<std::string> inverted(std::vector<std::string> moves) const {
    std::reverse(moves.begin(), moves.end());
    for (auto &move : moves)
      move = definition_.operations.at(definition_.operation_ids.at(move)).inverse;
    return moves;
  }
  void append(std::vector<std::string> &target, const std::vector<std::string> &source) {
    if (target.size() + source.size() > 10000)
      fail("Expanded algorithm exceeds 10000 primitives.");
    target.insert(target.end(), source.begin(), source.end());
  }
  std::vector<std::string> sequence(const std::string &terminators) {
    if (++depth_ > 64)
      fail("Notation nesting exceeds 64 levels.");
    std::vector<std::string> result;
    while (true) {
      skip();
      if (at_ == text_.size() || terminators.find(text_[at_]) != std::string::npos)
        break;
      std::vector<std::string> term;
      const auto character = text_[at_];
      if (character == '(') {
        ++at_;
        term = sequence(")");
        skip();
        if (at_ == text_.size() || text_[at_] != ')')
          fail("Missing closing parenthesis.");
        ++at_;
      } else if (character == '[') {
        ++at_;
        auto a = sequence(",:]");
        skip();
        if (at_ == text_.size() || (text_[at_] != ',' && text_[at_] != ':'))
          fail("Expected a commutator comma or conjugate colon.");
        const auto separator = text_[at_++];
        auto b = sequence("]");
        skip();
        if (at_ == text_.size() || text_[at_] != ']')
          fail("Missing closing bracket.");
        ++at_;
        append(term, a);
        append(term, b);
        append(term, inverted(a));
        if (separator == ',')
          append(term, inverted(b));
      } else {
        const auto start = at_;
        while (at_ < text_.size() &&
               (std::isalpha(static_cast<unsigned char>(text_[at_])) || text_[at_] == '_' ||
                text_[at_] == '/' || text_[at_] == '.' || text_[at_] == '-'))
          ++at_;
        if (start == at_)
          fail("Expected an operation, group, or bracketed expression.");
        const auto operation = text_.substr(start, at_ - start);
        if (!definition_.operation_ids.contains(operation))
          fail("Unknown directed operation: " + operation);
        term.push_back(operation);
      }
      skip();
      bool reverse = false;
      std::size_t repetitions = 1;
      if (at_ < text_.size() && text_[at_] == '\'') {
        reverse = true;
        ++at_;
      }
      if (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) {
        repetitions = 0;
        while (at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_]))) {
          repetitions = repetitions * 10 + static_cast<std::size_t>(text_[at_++] - '0');
          if (repetitions > 10000)
            fail("Repetition exceeds 10000.");
        }
        if (repetitions == 0)
          fail("Repetition must be positive.");
        if (at_ < text_.size() && text_[at_] == '\'') {
          reverse = !reverse;
          ++at_;
        }
      }
      if (reverse)
        term = inverted(std::move(term));
      for (std::size_t i = 0; i < repetitions; ++i)
        append(result, term);
    }
    --depth_;
    return result;
  }
};
} // namespace
std::vector<std::string> parse_notation(const Definition &definition, const std::string &text) {
  if (text.size() > 1024 * 1024)
    throw DiagnosticError("notation.limit", "/notation", "Notation input is limited to 1 MiB.");
  return Parser(definition, text).parse();
}
} // namespace twisty
