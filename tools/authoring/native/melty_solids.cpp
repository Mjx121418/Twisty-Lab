// Offline separating-axis checks for the fixed convex Melty authoring cells.
// This tool is never linked into the portable kernel or the web application.
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using V = std::array<double, 3>;
double dot(V a, V b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V cross(V a, V b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
struct Solid {
  std::vector<V> vertices, normals, edges;
};
bool separated(const Solid &a, const Solid &b, V axis) {
  const double length = std::sqrt(dot(axis, axis));
  if (length < 1e-10)
    return false;
  double loA = std::numeric_limits<double>::infinity(), hiA = -loA, loB = loA, hiB = hiA;
  for (auto v : a.vertices) {
    const auto s = dot(axis, v);
    loA = std::min(loA, s);
    hiA = std::max(hiA, s);
  }
  for (auto v : b.vertices) {
    const auto s = dot(axis, v);
    loB = std::min(loB, s);
    hiB = std::max(hiB, s);
  }
  return std::min(hiA, hiB) - std::max(loA, loB) <= 1e-9 * length;
}
bool overlap(const Solid &a, const Solid &b) {
  for (auto n : a.normals)
    if (separated(a, b, n))
      return false;
  for (auto n : b.normals)
    if (separated(a, b, n))
      return false;
  for (auto x : a.edges)
    for (auto y : b.edges)
      if (separated(a, b, cross(x, y)))
        return false;
  return true;
}
int main() {
  unsigned count;
  if (!(std::cin >> count) || count != 27)
    return 2;
  std::vector<std::vector<Solid>> pieces(count);
  for (auto &piece : pieces) {
    unsigned size;
    std::cin >> size;
    if (size > 1024)
      return 2;
    piece.resize(size);
    for (auto &s : piece)
      for (auto *values : {&s.vertices, &s.normals, &s.edges}) {
        unsigned n;
        std::cin >> n;
        if (n > 256)
          return 2;
        values->resize(n);
        for (auto &v : *values)
          for (auto &x : v)
            if (!(std::cin >> x) || !std::isfinite(x))
              return 2;
      }
  }
  for (unsigned p = 0; p < count; ++p)
    for (unsigned r = p + 1; r < count; ++r)
      for (unsigned a = 0; a < pieces[p].size(); ++a)
        for (unsigned b = 0; b < pieces[r].size(); ++b)
          if (overlap(pieces[p][a], pieces[r][b]))
            std::cout << p << ' ' << a << ' ' << r << ' ' << b << '\n';
}
