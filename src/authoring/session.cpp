#include "twisty/session.hpp"

namespace twisty {
Session::Session(const std::string &source) : Session(Execution::from_source(source)) {}
} // namespace twisty
