#pragma once
#include "twisty/catalog.hpp"

namespace twisty {
// Owned discovery document. Registry versions are independent of puzzle,
// envelope, backend payload and buffer versions. No compiler/UI is required.
Json geometry_backend_info();
} // namespace twisty
