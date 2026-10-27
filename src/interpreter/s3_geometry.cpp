#include "geometry_program.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <set>

namespace twisty {
struct S3Geometry {
  struct Asset {
    std::vector<GeometryPoint4> vertices, normals;
    std::vector<std::array<Index, 3>> triangles;
  };
  std::vector<Asset> assets;
  std::vector<Index> part_assets, active;
  double depth;
};
namespace {
using P = GeometryPoint4;
using M = GeometryMatrix;
void ensure(bool ok, const std::string &where, const std::string &message) {
  if (!ok)
    throw DiagnosticError("realization.s3", where, message);
}
void projection_ok(bool ok, const std::string &message) {
  if (!ok)
    throw DiagnosticError("geometry.projection", "/options", message);
}
double dot(const P &a, const P &b) {
  double out = 0;
  for (int i = 0; i < 4; ++i)
    out += a[i] * b[i];
  return out;
}
P unit(P p) {
  const double n = std::sqrt(dot(p, p));
  ensure(std::isfinite(n) && n > 1e-12, "/vector", "Expected a nonzero finite vector.");
  for (auto &v : p)
    v /= n;
  return p;
}
P point(const Json &j) {
  ensure(j.is_array() && j.size() == 4, "/vector", "Expected four coordinates.");
  P p{};
  for (int i = 0; i < 4; ++i) {
    ensure(j[i].is_number(), "/vector", "Expected numeric coordinates.");
    p[i] = j[i].get<double>();
    ensure(std::isfinite(p[i]) && std::abs(p[i]) <= 2, "/vector", "Invalid unit-vector coordinate.");
  }
  return p;
}
M identity() {
  M m{};
  for (int i = 0; i < 4; ++i)
    m[i * 5] = 1;
  return m;
}
P transform(const double *m, const P &p) {
  P out{};
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i)
      out[i] += m[j * 4 + i] * p[j];
  return out;
}
P column(const M &m, int j) {
  return {m[j * 4], m[j * 4 + 1], m[j * 4 + 2], m[j * 4 + 3]};
}
double determinant(const M &m) {
  std::array<int, 4> p{0, 1, 2, 3};
  double value = 0;
  do {
    int inversions = 0;
    for (int i = 0; i < 4; ++i)
      for (int j = i + 1; j < 4; ++j)
        inversions += p[i] > p[j];
    double term = inversions % 2 ? -1 : 1;
    for (int i = 0; i < 4; ++i)
      term *= m[i * 4 + p[i]];
    value += term;
  } while (std::next_permutation(p.begin(), p.end()));
  return value;
}
M matrix(const Json &j) {
  ensure(j.is_array() && j.size() == 16, "/frame", "Expected a column-major SO(4) matrix.");
  M m{};
  for (int i = 0; i < 16; ++i) {
    ensure(j[i].is_number(), "/frame", "Expected a finite SO(4) matrix.");
    m[i] = j[i].get<double>();
    ensure(std::isfinite(m[i]) && std::abs(m[i]) <= 1 + 1e-8, "/frame", "Invalid SO(4) matrix entry.");
  }
  for (int i = 0; i < 4; ++i)
    for (int k = 0; k < 4; ++k)
      ensure(std::abs(dot(column(m, i), column(m, k)) - (i == k ? 1 : 0)) < 1e-8, "/frame",
             "Frame is not orthogonal.");
  ensure(std::abs(determinant(m) - 1) < 1e-8, "/frame", "Frame must preserve orientation.");
  return m;
}
P cell(int c) {
  ensure(c && std::abs(c) <= 4, "/cell", "Expected one of the eight signed coordinate cells.");
  P p{};
  p[std::abs(c) - 1] = c > 0 ? 1 : -1;
  return p;
}
std::string cell_name(int c) {
  return std::string(1, "XYZW"[std::abs(c) - 1]) + (c > 0 ? "p" : "m");
}
int cell_number(const std::string &name) {
  for (int i = 1; i <= 4; ++i)
    for (int s : {-1, 1})
      if (cell_name(s * i) == name)
        return s * i;
  throw DiagnosticError("realization.s3", "/cell", "Unknown cell label.");
}
M focus(int c) {
  auto m = identity();
  const int i = std::abs(c) - 1, s = c > 0 ? 1 : -1;
  if (i == 3) {
    if (s > 0)
      m[0] = m[15] = -1;
  } else {
    m[i * 5] = m[15] = 0;
    m[i * 4 + 3] = -s;
    m[12 + i] = s;
  }
  return m;
}
GeometryPoint project(const P &p, double scale) {
  return {scale * p[0] / (1 - p[3]), scale * p[1] / (1 - p[3]), scale * p[2] / (1 - p[3])};
}
GeometryPoint projected_normal(const P &p, const P &n) {
  GeometryPoint out{};
  double norm = 0;
  for (int i = 0; i < 3; ++i) {
    out[i] = (1 - p[3]) * n[i] + p[i] * n[3];
    norm += out[i] * out[i];
  }
  norm = std::sqrt(norm);
  ensure(norm > 1e-14, "/normal", "Degenerate projected normal.");
  for (auto &x : out)
    x /= norm;
  return out;
}
double winding(const GeometryPoint &a, const GeometryPoint &b, const GeometryPoint &c,
               const GeometryPoint &n) {
  const GeometryPoint u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  return (u[1] * v[2] - u[2] * v[1]) * n[0] + (u[2] * v[0] - u[0] * v[2]) * n[1] +
         (u[0] * v[1] - u[1] * v[0]) * n[2];
}
// Canonical endpoint frames may differ by an unmarked rotation of the inactive
// coordinates. Certify the colored triangulation as well as its occupied cell.
void verify_stabilizer(const S3Geometry::Asset &asset, int active, const std::string &id) {
  using V = std::array<long long, 8>;
  using T = std::array<V, 3>;
  auto triangles = [&](const M &m) {
    std::vector<V> vertices;
    for (std::size_t i = 0; i < asset.vertices.size(); ++i) {
      const auto p = transform(m.data(), asset.vertices[i]), n = transform(m.data(), asset.normals[i]);
      V v{};
      for (int j = 0; j < 4; ++j) {
        v[j] = std::llround(p[j] * 1e8);
        v[j + 4] = std::llround(n[j] * 1e8);
      }
      vertices.push_back(v);
    }
    std::multiset<T> result;
    for (const auto &t : asset.triangles) {
      T triangle{vertices[t[0]], vertices[t[1]], vertices[t[2]]};
      std::sort(triangle.begin(), triangle.end());
      result.insert(triangle);
    }
    return result;
  };
  const auto original = triangles(identity());
  std::array<int, 4> permutation{0, 1, 2, 3};
  do {
    bool fixes = true;
    for (int i = 0; i < active; ++i)
      fixes &= permutation[i] == i;
    if (!fixes)
      continue;
    for (int signs = 0; signs < (1 << (4 - active)); ++signs) {
      M m{};
      for (int i = 0; i < 4; ++i)
        m[4 * i + permutation[i]] = i < active || !(signs & (1 << (i - active))) ? 1 : -1;
      if (determinant(m) < 0)
        continue;
      ensure(triangles(m) == original, id,
             "Colored mesh is not invariant under its unmarked endpoint stabilizer.");
    }
  } while (std::next_permutation(permutation.begin(), permutation.end()));
}
} // namespace

std::shared_ptr<const GeometryProgram> compile_s3(std::shared_ptr<const Execution> execution, const Json &r) {
  const auto definition = execution->definition();
  ensure(r.at("schemaVersion") == 1 && r.at("compatibleDefinitionDigest") == definition->digest,
         "/compatibleDefinitionDigest", "S3 realization does not match this definition.");
  const std::set<std::string> capabilities{"triangle-meshes", "so4-isometries", "stereographic-projection",
                                           "dynamic-drawing-meshes"};
  ensure(r.at("requiredCapabilities").get<std::set<std::string>>() == capabilities, "/requiredCapabilities",
         "Declare all S3 drawing capabilities.");
  auto program = std::make_shared<GeometryProgram>();
  auto s3 = std::make_shared<S3Geometry>();
  program->s3 = s3;
  program->execution = std::move(execution);
  s3->depth = r.at("cutDepth").get<double>();
  ensure(std::isfinite(s3->depth) && s3->depth > 0 && s3->depth < .5, "/cutDepth",
         "Cut depth must be between zero and one half.");
  const double inset = r.at("pieceInset").get<double>();
  ensure(std::isfinite(inset) && inset >= 0 && inset < 1, "/pieceInset", "Invalid intrinsic piece inset.");
  ensure(definition->pieces.size() <= 4096 && definition->domains.size() <= 64 &&
             definition->operations.size() <= 4096,
         "/definition", "S3 definition exceeds geometry limits.");
  program->space = {{"id", "S3"},
                    {"intrinsicDimension", 3},
                    {"coordinateModel", "unit-sphere-in-euclidean-4"},
                    {"metric", "spherical"},
                    {"curvature", 1},
                    {"radius", 1},
                    {"pointCoordinates", 4},
                    {"embeddingDimension", 4},
                    {"metricSignature", {1, 1, 1, 1}},
                    {"homogeneous", false},
                    {"isometryRows", 4},
                    {"isometryColumns", 4},
                    {"isometryScalar", "float64"},
                    {"matrixOrder", "column-major"},
                    {"meshCoordinates", "unit-sphere"}};
  program->scene = {{"sceneId", r.at("id").get<std::string>() + ":" + definition->digest},
                    {"realizationId", r.at("id")},
                    {"definitionDigest", definition->digest},
                    {"requiredCapabilities", r.at("requiredCapabilities")},
                    {"space", program->space},
                    {"projections", {"stereographic"}},
                    {"drawingSpace", "R3"},
                    {"drawingBufferVersion", 1},
                    {"ambientSpace", "S3"},
                    {"dynamicMeshes", true},
                    {"diagram", false},
                    {"fidelity", "spherical-eight-cell"},
                    {"surfaceTransportVerified", true},
                    {"controlLayout", r.at("controlLayout")},
                    {"meshAssets", Json::array()},
                    {"visualParts", Json::array()},
                    {"labels", Json::array()}};
  std::map<std::string, Index> asset_ids;
  ensure(r.at("assets").is_object() && r.at("assets").size() <= 64, "/assets", "Too many S3 prototypes.");
  std::size_t vertices = 0;
  for (const auto &[id, j] : r.at("assets").items()) {
    S3Geometry::Asset a;
    ensure(j.at("vertices").size() == j.at("normals").size() && !j.at("vertices").empty(), id,
           "Each vertex needs a tangent normal.");
    vertices += j.at("vertices").size();
    ensure(vertices <= 100000 && j.at("triangles").size() <= 200000, id, "S3 asset limit exceeded.");
    for (std::size_t i = 0; i < j.at("vertices").size(); ++i) {
      const auto p = point(j.at("vertices")[i]), n = point(j.at("normals")[i]);
      ensure(std::abs(dot(p, p) - 1) < 1e-8 && std::abs(dot(n, n) - 1) < 1e-8 && std::abs(dot(p, n)) < 1e-8,
             id, "Expected unit S3 points and unit tangent outward normals.");
      a.vertices.push_back(p);
      a.normals.push_back(n);
    }
    for (const auto &t : j.at("triangles")) {
      ensure(t.is_array() && t.size() == 3, id, "Expected a triangle.");
      std::array<Index, 3> triangle{};
      for (int i = 0; i < 3; ++i) {
        ensure(t[i].is_number_unsigned() || (t[i].is_number_integer() && t[i] >= 0), id,
               "Invalid vertex index.");
        ensure(t[i].get<std::uint64_t>() < a.vertices.size(), id, "Vertex index outside asset.");
        triangle[i] = t[i].get<Index>();
      }
      for (int i = 0; i < 3; ++i)
        ensure(dot(a.vertices[triangle[i]], a.vertices[triangle[(i + 1) % 3]]) > .8, id,
               "S3 triangles require finer tessellation.");
      // Prototype points are away from this chart's pole. Winding is later
      // checked again after projection, including arbitrary view rotations.
      a.triangles.push_back(triangle);
    }
    ensure(!a.triangles.empty(), id, "Empty S3 mesh.");
    asset_ids.emplace(id, static_cast<Index>(s3->assets.size()));
    s3->assets.push_back(std::move(a));
  }
  std::vector<int> cells;
  std::vector<GeometryRotation> paths;
  ensure(r.at("operationTracks").size() == definition->operations.size(), "/operationTracks",
         "Every operation needs a track.");
  for (const auto &op : definition->operations) {
    const auto &j = r.at("operationTracks").at(op.id);
    const int c = j.at("cell").get<int>();
    const auto n = cell(c), u = point(j.at("planeU")), v = point(j.at("planeV"));
    const double angle = j.at("angle").get<double>();
    ensure(std::isfinite(angle) && std::abs(angle) > 1e-8 && std::abs(angle) <= std::numbers::pi + 1e-8,
           op.id, "Expected a nonzero directed cell turn of at most pi.");
    ensure(std::abs(dot(u, u) - 1) + std::abs(dot(v, v) - 1) + std::abs(dot(u, v)) + std::abs(dot(n, u)) +
                   std::abs(dot(n, v)) <
               1e-8,
           op.id, "Rotation plane must be orthonormal and tangent to the selected cell.");
    ensure(op.family == cell_name(c), op.id, "Operation family must identify its cell.");
    GeometryRotation path{{}, {}, angle, false, std::array<P, 2>{u, v}};
    const auto endpoint = geometry_rotation(path, 1);
    for (double x : endpoint)
      ensure(std::abs(x - std::round(x)) < 1e-8, op.id,
             "Cell turns must preserve the signed-coordinate cuts.");
    cells.push_back(c);
    paths.push_back(path);
  }
  const auto &layout = r.at("controlLayout");
  ensure(layout.at("version") == 2 && layout.at("kind") == "cell-axes" && layout.at("groups").size() == 8,
         "/controlLayout", "Expected eight cell groups.");
  std::set<std::string> shown, groups;
  for (const auto &group : layout.at("groups")) {
    const auto id = group.at("id").get<std::string>();
    const auto center = cell(cell_number(id)), actual = point(group.at("center"));
    ensure(groups.insert(id).second && dot(center, actual) > 1 - 1e-9, id, "Invalid or repeated cell group.");
    ensure(group.at("categories").size() == 3, id, "Expected three twist categories.");
    std::set<std::string> kinds, axes;
    for (const auto &category : group.at("categories")) {
      const auto kind = category.at("kind").get<std::string>();
      const int count = kind == "face" ? 3 : kind == "edge" ? 6 : kind == "vertex" ? 4 : 0;
      ensure(count && kinds.insert(kind).second &&
                 category.at("axes").size() == static_cast<std::size_t>(count),
             id, "Invalid twist category.");
      for (const auto &axis : category.at("axes")) {
        const auto direction = point(axis.at("direction"));
        ensure(axes.insert(axis.at("id").get<std::string>()).second &&
                   std::abs(dot(direction, direction) - 1) + std::abs(dot(center, direction)) < 1e-8,
               id, "Invalid tangent axis.");
        const int components = kind == "face" ? 1 : kind == "edge" ? 2 : 3;
        int nonzero = 0;
        for (double x : direction)
          if (std::abs(x) > 1e-8) {
            ++nonzero;
            ensure(std::abs(std::abs(x) - 1 / std::sqrt(components)) < 1e-8, id,
                   "Axis does not match its category.");
          }
        ensure(nonzero == components, id, "Axis does not match its category.");
        const int turns = kind == "face" ? 3 : kind == "edge" ? 1 : 2;
        ensure(axis.at("turns").size() == static_cast<std::size_t>(turns), id, "Invalid angle choices.");
        std::set<int> angles;
        for (const auto &turn : axis.at("turns")) {
          const double degrees = turn.at("angleDegrees").get<double>();
          ensure(std::abs(degrees - std::round(degrees)) < 1e-8, id, "Expected an exact angle choice.");
          angles.insert(static_cast<int>(std::round(degrees)));
          const auto name = turn.at("operation").get<std::string>();
          ensure(definition->operation_ids.contains(name) && shown.insert(name).second, id,
                 "Missing or repeated turn.");
          const auto op = definition->operation_ids.at(name);
          const auto &path = paths[op];
          ensure(definition->operations[op].family == id &&
                     std::abs(path.angle - turn.at("angleDegrees").get<double>() * std::numbers::pi / 180) <
                         1e-9 &&
                     std::abs(dot(direction, path.plane->at(0))) +
                             std::abs(dot(direction, path.plane->at(1))) <
                         1e-8,
                 name, "Controller axis/angle does not match the motion.");
        }
        ensure(angles == (kind == "face"   ? std::set<int>{-90, 90, 180}
                          : kind == "edge" ? std::set<int>{180}
                                           : std::set<int>{-120, 120}),
               id, "Invalid category angles.");
      }
    }
  }
  ensure(shown.size() == definition->operations.size(), "/controlLayout",
         "Controller must expose every turn.");
  ensure(r.at("domains").size() == definition->domains.size(), "/domains", "Missing S3 domain.");
  std::vector<std::map<std::string, Index>> port_assets;
  std::vector<int> active;
  std::set<std::pair<Index, int>> verified_assets;
  for (Index d = 0; d < definition->domains.size(); ++d) {
    const auto &domain = definition->domains[d];
    const auto &data = r.at("domains").at(domain.id);
    const int k = data.at("activeCoordinates").get<int>();
    ensure(k >= 1 && k <= 4, domain.id, "Invalid active coordinates.");
    active.push_back(k);
    ensure(data.at("placementFrames").size() == domain.placements.size() && domain.placements.size() <= 1024,
           domain.id, "Frame count differs from placements.");
    GeometryPoseTable table;
    for (const auto &q : domain.placements) {
      const auto m = matrix(data.at("placementFrames").at(q.key));
      for (double x : m)
        ensure(std::abs(x - std::round(x)) < 1e-8, domain.id,
               "Resting frames must be signed coordinate permutations.");
      ensure(q.ports.size() == static_cast<std::size_t>(k), domain.id,
             "Port count differs from piece colors.");
      for (int j = 0; j < k; ++j)
        ensure(dot(column(m, j), cell(cell_number(q.ports.at(std::to_string(j)).second))) > 1 - 1e-9,
               domain.id, "S3 frame differs from its colored attachment.");
      table.frames.push_back(m);
    }
    for (Index i = 0; i < definition->operations.size(); ++i) {
      const auto &op = definition->operations[i];
      const auto n = cell(cells[i]);
      const auto end = geometry_rotation(paths[i], 1);
      for (Index q = 0; q < domain.placements.size(); ++q) {
        bool participates = false;
        for (int j = 0; j < k; ++j)
          participates |= dot(column(table.frames[q], j), n) > 1 - 1e-8;
        ensure(op.roles[d][q] == (participates ? 1 : 0), op.id,
               "Abstract participation must equal the complete cutting ball.");
        if (!participates)
          continue;
        const auto &target = table.frames.at(op.maps[d][q]);
        for (int j = 0; j < k; ++j)
          ensure(dot(transform(end.data(), column(table.frames[q], j)), column(target, j)) > 1 - 1e-8, op.id,
                 "S3 endpoint differs from the colored transport.");
      }
      GeometryTrack track;
      track.uniform = {paths[i]};
      table.tracks.push_back(std::move(track));
    }
    program->tables.push_back(std::move(table));
    std::map<std::string, Index> bindings;
    ensure(data.at("ports").size() == static_cast<std::size_t>(k), domain.id, "Missing colored mesh.");
    for (int j = 0; j < k; ++j) {
      const auto port = std::to_string(j), asset = data.at("ports").at(port).get<std::string>();
      ensure(asset_ids.contains(asset), asset, "Unknown S3 mesh.");
      bindings[port] = asset_ids.at(asset);
      if (verified_assets.emplace(bindings[port], k).second)
        verify_stabilizer(s3->assets[bindings[port]], k, asset);
      const auto &mesh = s3->assets[bindings[port]];
      P center{};
      for (int a = 0; a < k; ++a)
        center[a] = 1 / std::sqrt(k);
      for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
        const auto &p = mesh.vertices[vertex];
        for (int a = 0; a < 4; ++a)
          ensure(a < k ? p[a] >= s3->depth - 1e-8 : std::abs(p[a]) <= s3->depth + 1e-8, asset,
                 "Mesh crosses a piece cut.");
        for (int a = 0; a < k; ++a)
          ensure(p[j] >= p[a] - 1e-8, asset, "Mesh crosses its color sector.");
        const double pc = dot(p, center);
        const double t = inset * pc + std::sqrt((1 - inset) * (1 - inset) - inset * inset * (1 - pc * pc));
        P original{};
        for (int a = 0; a < 4; ++a)
          original[a] = (t * p[a] - inset * center[a]) / (1 - inset);
        bool outward = false;
        for (int a = 0; a < 4; ++a) {
          const double h = a < k ? s3->depth : -s3->depth;
          const double direction = a < k ? 1 : original[a] > 0 ? -1 : 1;
          if (std::abs(direction * original[a] - h) > 1e-8)
            continue;
          P n{};
          for (int b = 0; b < 4; ++b)
            n[b] = h * original[b] - (a == b ? direction : 0);
          const double adjustment = dot(n, p) / dot(original, p);
          for (int b = 0; b < 4; ++b)
            n[b] -= adjustment * original[b];
          outward |= dot(unit(n), mesh.normals[vertex]) > 1 - 1e-8;
        }
        ensure(outward, asset, "Vertex or outward normal differs from its inset cut surface.");
      }
    }
    port_assets.push_back(std::move(bindings));
  }
  for (Index p = 0; p < definition->pieces.size(); ++p) {
    const auto &piece = definition->pieces[p];
    for (const auto &[port, color] : piece.labels) {
      const auto id = piece.id + "/" + port;
      ensure(port_assets[piece.domain].contains(port), id, "Missing piece color mesh.");
      program->part_ids[id] = static_cast<Index>(program->parts.size());
      program->parts.push_back({p, piece.domain, 1});
      s3->part_assets.push_back(port_assets[piece.domain].at(port));
      s3->active.push_back(active[piece.domain]);
      program->scene["visualParts"].push_back({{"visualPartId", id},
                                               {"pieceId", piece.id},
                                               {"portId", port},
                                               {"meshAssetId", id},
                                               {"materialBindingId", color},
                                               {"role", "port"}});
    }
  }
  std::vector<double> poses;
  for (const auto &part : program->parts) {
    const auto &m = program->tables[part.table].frames[definition->initial.placement_of[part.piece]];
    poses.insert(poses.end(), m.begin(), m.end());
  }
  const auto drawing = project_s3(*program, poses, Json::object());
  program->positions = drawing->positions;
  program->normals = drawing->normals;
  program->indices = drawing->indices;
  program->scene = drawing->scene;
  return program;
}

std::shared_ptr<const ProjectedMesh> project_s3(const GeometryProgram &program,
                                                const std::vector<double> &poses, const Json &options) {
  const std::set<std::string> allowed{"version", "viewIsometry", "focusCell", "scale", "clipRadius", "guide"};
  for (const auto &[key, value] : options.items()) {
    (void)value;
    projection_ok(allowed.contains(key), "Unknown projection option: " + key);
  }
  projection_ok(options.value("version", 1) == 1, "Unsupported projection options version.");
  projection_ok(!(options.contains("viewIsometry") && options.contains("focusCell")),
                "Use either focusCell or viewIsometry.");
  auto view = identity();
  try {
    if (options.contains("viewIsometry"))
      view = matrix(options.at("viewIsometry"));
    else
      view = focus(cell_number(options.value("focusCell", std::string{"Wm"})));
  } catch (const std::exception &) {
    throw DiagnosticError("geometry.projection", "/viewIsometry", "Invalid SO(4) view or focus cell.");
  }
  const double scale = options.value("scale", 1.), radius = options.value("clipRadius", 8.);
  projection_ok(std::isfinite(scale) && scale >= .01 && scale <= 20 && std::isfinite(radius) &&
                    radius >= .5 && radius <= 32,
                "Projection scale or clipping radius is outside its supported range.");
  const double clip = (radius * radius - 1) / (radius * radius + 1);
  auto out = std::make_shared<ProjectedMesh>();
  out->scene = program.scene;
  out->scene["meshAssets"] = Json::array();
  out->scene.erase("axisGuide");
  out->scene.erase("guidePieces");
  std::size_t clipped = 0;
  std::set<std::string> outer;
  Json piece_depths = Json::object();
  const auto &s3 = *program.s3;
  struct Vertex {
    P p, n;
  };
  auto blend = [](const Vertex &a, const Vertex &b, double t) {
    Vertex v;
    for (int i = 0; i < 4; ++i) {
      v.p[i] = (1 - t) * a.p[i] + t * b.p[i];
      v.n[i] = (1 - t) * a.n[i] + t * b.n[i];
    }
    v.p = unit(v.p);
    const double n = dot(v.n, v.p);
    for (int i = 0; i < 4; ++i)
      v.n[i] -= n * v.p[i];
    v.n = unit(v.n);
    return v;
  };
  for (Index part = 0; part < program.parts.size(); ++part) {
    const auto &asset = s3.assets[s3.part_assets[part]];
    const auto pose = poses.data() + 16 * part;
    const std::size_t position_start = out->positions.size(), index_start = out->indices.size();
    std::vector<Vertex> vertices;
    vertices.reserve(asset.vertices.size());
    for (std::size_t i = 0; i < asset.vertices.size(); ++i)
      vertices.push_back({transform(view.data(), transform(pose, asset.vertices[i])),
                          transform(view.data(), transform(pose, asset.normals[i]))});
    P pole{};
    for (int j = 0; j < 4; ++j) {
      P col{pose[j * 4], pose[j * 4 + 1], pose[j * 4 + 2], pose[j * 4 + 3]};
      pole[j] = transform(view.data(), col)[3];
    }
    double depth = 0;
    for (Index j = 0; j < s3.active[part]; ++j)
      depth += pole[j];
    piece_depths[out->scene["visualParts"][part]["pieceId"].get<std::string>()] =
        depth / std::sqrt(s3.active[part]);
    bool encloses = true;
    for (int j = 0; j < 4; ++j)
      encloses &= j < static_cast<int>(s3.active[part]) ? pole[j] > s3.depth : std::abs(pole[j]) < s3.depth;
    if (encloses)
      outer.insert(out->scene["visualParts"][part]["pieceId"].get<std::string>());
    auto upper = [](const Vertex &a, const Vertex &b, const Vertex &c) {
      const double largest = std::max({a.p[3], b.p[3], c.p[3]});
      return largest <= 0
                 ? largest
                 : largest /
                       std::sqrt(std::max(1e-12, std::min({dot(a.p, b.p), dot(b.p, c.p), dot(c.p, a.p)})));
    };
    bool safe = true;
    for (const auto &t : asset.triangles)
      if (upper(vertices[t[0]], vertices[t[1]], vertices[t[2]]) > clip) {
        safe = false;
        break;
      }
    auto append = [&](const Vertex &v) {
      projection_ok(v.p[3] <= clip + 1e-10 && out->positions.size() / 3 < 1000000,
                    "Projected mesh exceeds clipping or vertex limits.");
      const auto p = project(v.p, scale), n = projected_normal(v.p, v.n);
      const auto id = static_cast<Index>((out->positions.size() - position_start) / 3);
      for (double x : p)
        out->positions.push_back(static_cast<float>(x));
      for (double x : n)
        out->normals.push_back(static_cast<float>(x));
      return id;
    };
    auto triangle = [&](Index a, Index b, Index c) {
      auto p = [&](Index i) {
        const auto offset = position_start + 3 * i;
        return GeometryPoint{out->positions[offset], out->positions[offset + 1], out->positions[offset + 2]};
      };
      const auto offset = position_start + 3 * a;
      const GeometryPoint n{out->normals[offset], out->normals[offset + 1], out->normals[offset + 2]};
      if (winding(p(a), p(b), p(c), n) < 0)
        std::swap(b, c);
      out->indices.insert(out->indices.end(), {a, b, c});
    };
    if (safe) {
      for (const auto &v : vertices)
        append(v);
      for (const auto &t : asset.triangles)
        triangle(t[0], t[1], t[2]);
    } else {
      std::function<void(Vertex, Vertex, Vertex, int)> emit;
      emit = [&](Vertex a, Vertex b, Vertex c, int depth) {
        const double low = std::min({a.p[3], b.p[3], c.p[3]});
        if (low > clip && low >= 0)
          return;
        if (upper(a, b, c) <= clip) {
          const auto i = append(a), j = append(b), k = append(c);
          triangle(i, j, k);
          return;
        }
        const bool all_inside = std::max({a.p[3], b.p[3], c.p[3]}) <= clip;
        if (all_inside || depth < 2 || clip < 0) {
          if (depth >= 8)
            return; // Conservative bounded approximation at the clip boundary.
          const auto ab = blend(a, b, .5), bc = blend(b, c, .5), ca = blend(c, a, .5);
          emit(a, ab, ca, depth + 1);
          emit(ab, b, bc, depth + 1);
          emit(ca, bc, c, depth + 1);
          emit(ab, bc, ca, depth + 1);
          return;
        }
        std::vector<Vertex> polygon;
        const std::array<Vertex, 3> t{a, b, c};
        for (int i = 0; i < 3; ++i) {
          const auto &x = t[i], &y = t[(i + 1) % 3];
          const bool xi = x.p[3] <= clip, yi = y.p[3] <= clip;
          if (xi)
            polygon.push_back(x);
          if (xi != yi) {
            double lo = 0, hi = 1;
            for (int k = 0; k < 40; ++k) {
              const double mid = (lo + hi) / 2;
              if ((blend(x, y, mid).p[3] <= clip) == xi)
                lo = mid;
              else
                hi = mid;
            }
            polygon.push_back(blend(x, y, xi ? lo : hi));
          }
        }
        if (polygon.size() >= 3) {
          const auto a0 = append(polygon[0]);
          for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            const auto b0 = append(polygon[i]), c0 = append(polygon[i + 1]);
            triangle(a0, b0, c0);
          }
        }
      };
      for (const auto &t : asset.triangles) {
        const auto &a = vertices[t[0]], &b = vertices[t[1]], &c = vertices[t[2]];
        clipped += upper(a, b, c) > clip;
        emit(a, b, c, 0);
      }
    }
    out->scene["meshAssets"].push_back({{"id", out->scene["visualParts"][part]["meshAssetId"]},
                                        {"positionOffset", position_start},
                                        {"normalOffset", position_start},
                                        {"vertexCount", (out->positions.size() - position_start) / 3},
                                        {"indexOffset", index_start},
                                        {"indexCount", out->indices.size() - index_start}});
  }
  out->scene["projection"] = {{"version", 1},           {"kind", "stereographic"},
                              {"viewIsometry", view},   {"scale", scale},
                              {"clipRadius", radius},   {"clippedTriangles", clipped},
                              {"outerPieceIds", outer}, {"pieceDepths", piece_depths}};
  if (options.contains("guide")) {
    const auto &guide = options.at("guide");
    projection_ok(guide.is_object() && guide.size() == 2 && guide.contains("group") && guide.contains("axis"),
                  "Invalid axis guide.");
    bool found = false;
    for (const auto &g : program.scene.at("controlLayout").at("groups"))
      if (g.at("id") == guide.at("group"))
        for (const auto &category : g.at("categories"))
          for (const auto &axis : category.at("axes"))
            if (axis.at("id") == guide.at("axis")) {
              found = true;
              const auto c = point(g.at("center")), u = point(axis.at("direction"));
              Json lines = Json::array(), line = Json::array(), pieces = Json::array();
              for (int i = 0; i <= 64; ++i) {
                const double angle = std::acos(s3.depth) * (2. * i / 64 - 1);
                P p{};
                for (int j = 0; j < 4; ++j)
                  p[j] = c[j] * std::cos(angle) + u[j] * std::sin(angle);
                p = transform(view.data(), p);
                if (p[3] <= clip)
                  line.push_back(project(p, scale));
                else if (!line.empty()) {
                  lines.push_back(line);
                  line = Json::array();
                }
              }
              if (!line.empty())
                lines.push_back(line);
              std::set<std::string> selected;
              for (Index i = 0; i < program.parts.size(); ++i)
                for (Index j = 0; j < s3.active[i]; ++j) {
                  const auto m = poses.data() + 16 * i;
                  if (dot(P{m[j * 4], m[j * 4 + 1], m[j * 4 + 2], m[j * 4 + 3]}, c) > 1 - 1e-8)
                    selected.insert(program.scene["visualParts"][i]["pieceId"].get<std::string>());
                }
              out->scene["axisGuide"] = lines;
              out->scene["guidePieces"] = selected;
            }
    projection_ok(found, "Unknown axis guide.");
  }
  return out;
}
} // namespace twisty
