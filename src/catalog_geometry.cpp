#include "twisty/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace twisty {
namespace {
using Frame = std::array<double, 16>;
using Point = std::array<double, 3>;
constexpr double tolerance = 1e-6;
Point add(Point a, Point b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Point sub(Point a, Point b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point scaled(Point a, double s) {
  return {a[0] * s, a[1] * s, a[2] * s};
}
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
void ensure(bool value, const std::string &source, const std::string &message) {
  if (!value)
    throw DiagnosticError("realization.catalog", source, message);
}
Point unit(Point a) {
  const auto length = std::sqrt(dot(a, a));
  ensure(std::isfinite(length) && length > 1e-12, "/geometry", "A geometric direction is degenerate.");
  return scaled(a, 1 / length);
}
bool close(Point a, Point b) {
  const auto d = sub(a, b);
  return dot(d, d) <= tolerance * tolerance;
}
Point column(const Frame &m, int i) {
  return {m[i * 4], m[i * 4 + 1], m[i * 4 + 2]};
}
Point transformed(const Frame &m, Point p) {
  return add(add(scaled(column(m, 0), p[0]), scaled(column(m, 1), p[1])), scaled(column(m, 2), p[2]));
}
Frame identity() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
Frame read_frame(const Json &json) {
  ensure(json.is_array() && json.size() == 9, "/placementFrames",
         "A frame must have nine row-major entries.");
  auto frame = identity();
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) {
      const auto value = json.at(row * 3 + col).get<double>();
      ensure(std::isfinite(value), "/placementFrames", "Frame entries must be finite.");
      frame[col * 4 + row] = value;
    }
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      ensure(std::abs(dot(column(frame, i), column(frame, j)) - (i == j ? 1 : 0)) <= 1e-10,
             "/placementFrames", "Placement frames must be orthonormal.");
  ensure(dot(column(frame, 0), cross(column(frame, 1), column(frame, 2))) > 1 - 1e-10, "/placementFrames",
         "Placement frames must be proper rotations.");
  return frame;
}
Point rotate(Point p, Point axis, double angle) {
  return add(add(scaled(p, std::cos(angle)), scaled(cross(axis, p), std::sin(angle))),
             scaled(axis, dot(axis, p) * (1 - std::cos(angle))));
}
Frame rotated(Frame frame, Point axis, double angle) {
  for (int col = 0; col < 3; ++col) {
    const auto v = rotate(column(frame, col), axis, angle);
    for (int row = 0; row < 3; ++row)
      frame[col * 4 + row] = v[row];
  }
  return frame;
}
Frame composed(Frame a, Frame b) {
  auto out = identity();
  for (int col = 0; col < 3; ++col) {
    const auto v = transformed(a, column(b, col));
    for (int row = 0; row < 3; ++row)
      out[col * 4 + row] = v[row];
  }
  return out;
}
bool close(Frame a, Frame b) {
  for (int col = 0; col < 3; ++col)
    if (!close(column(a, col), column(b, col)))
      return false;
  return true;
}
} // namespace

void Geometry::build_catalog(const Json &realization) {
  ensure(definition_->placement_relations, "/ruleModule", "Polyhedral catalogs require placement relations.");
  scale_ = realization.at("scale").get<double>();
  const auto body_inset = realization.at("bodyInset").get<double>();
  const auto sticker_inset = realization.at("stickerInset").get<double>();
  const auto lift = realization.at("stickerLift").get<double>();
  ensure(std::isfinite(scale_) && scale_ > 0 && std::isfinite(body_inset) && body_inset >= 0 &&
             body_inset < 0.5 && std::isfinite(sticker_inset) && sticker_inset >= 0 && sticker_inset < 0.5 &&
             std::isfinite(lift) && lift >= 0,
         "/models", "Visual scales and clearances are invalid.");
  std::map<std::string, Index> model_ids;
  // Assets have local indices and are packed into the existing bridge buffers.
  auto asset = [&](const std::string &id, const auto &emit) {
    const auto position_start = positions_.size(), index_start = indices_.size();
    auto triangle = [&](Point a, Point b, Point c, Point n) {
      if (dot(cross(sub(b, a), sub(c, a)), n) < 0)
        std::swap(b, c);
      const auto first = static_cast<std::uint32_t>((positions_.size() - position_start) / 3);
      for (const auto &v : {a, b, c}) {
        for (double x : v)
          positions_.push_back(static_cast<float>(x));
        for (double x : n)
          normals_.push_back(static_cast<float>(x));
      }
      indices_.insert(indices_.end(), {first, first + 1, first + 2});
    };
    emit(triangle);
    scene_["meshAssets"].push_back({{"id", id},
                                    {"positionOffset", position_start},
                                    {"vertexCount", (positions_.size() - position_start) / 3},
                                    {"indexOffset", index_start},
                                    {"indexCount", indices_.size() - index_start}});
  };
  for (const auto &[id, data] : realization.at("models").items()) {
    Model model;
    model.id = id;
    model.vertices = data.at("vertices").get<std::vector<Point>>();
    ensure(model.vertices.size() >= 4 && model.vertices.size() <= 32, id,
           "A polyhedron needs 4–32 vertices.");
    Point center{};
    for (const auto &v : model.vertices) {
      for (double x : v)
        ensure(std::isfinite(x), id, "Vertices must be finite.");
      center = add(center, scaled(v, 1.0 / model.vertices.size()));
    }
    auto inset = [&](Point v) { return add(center, scaled(sub(v, center), 1 - body_inset)); };
    std::map<std::vector<Index>, Point> facets;
    for (Index a = 0; a < model.vertices.size(); ++a)
      for (Index b = a + 1; b < model.vertices.size(); ++b)
        for (Index c = b + 1; c < model.vertices.size(); ++c) {
          const auto raw =
              cross(sub(model.vertices[b], model.vertices[a]), sub(model.vertices[c], model.vertices[a]));
          if (dot(raw, raw) < 1e-20)
            continue;
          auto n = unit(raw);
          double minimum = 0, maximum = 0;
          std::vector<Index> face;
          for (Index v = 0; v < model.vertices.size(); ++v) {
            const auto d = dot(n, sub(model.vertices[v], model.vertices[a]));
            minimum = std::min(minimum, d);
            maximum = std::max(maximum, d);
            if (std::abs(d) < 1e-10)
              face.push_back(v);
          }
          if (minimum < -1e-10 && maximum > 1e-10)
            continue;
          if (minimum >= -1e-10)
            n = scaled(n, -1);
          facets.emplace(face, n);
        }
    ensure(facets.size() >= 4, id, "A model must enclose a three-dimensional volume.");
    if (!diagram_ && !catalog_spherical_)
      asset(id + "/body", [&](const auto &triangle) {
        for (const auto &[vertices, n] : facets) {
          Point face_center{};
          for (auto v : vertices)
            face_center = add(face_center, scaled(model.vertices[v], 1.0 / vertices.size()));
          const auto r = unit(sub(model.vertices[vertices[0]], face_center)), u = cross(n, r);
          auto sorted = vertices;
          std::sort(sorted.begin(), sorted.end(), [&](Index a, Index b) {
            const auto da = sub(model.vertices[a], face_center), db = sub(model.vertices[b], face_center);
            return std::atan2(dot(da, u), dot(da, r)) < std::atan2(dot(db, u), dot(db, r));
          });
          for (std::size_t v = 1; v + 1 < sorted.size(); ++v)
            triangle(inset(model.vertices[sorted[0]]), inset(model.vertices[sorted[v]]),
                     inset(model.vertices[sorted[v + 1]]), n);
        }
      });
    for (const auto &[port, vertices] : data.at("ports").items()) {
      auto face = vertices.get<std::vector<Index>>();
      ensure(face.size() >= 3 && face.size() <= 32 &&
                 std::set<Index>(face.begin(), face.end()).size() == face.size() &&
                 std::all_of(face.begin(), face.end(), [&](Index v) { return v < model.vertices.size(); }),
             id + "/" + port, "A port must name 3–32 distinct model vertices.");
      Point anchor{};
      for (auto v : face)
        anchor = add(anchor, scaled(model.vertices[v], 1.0 / face.size()));
      // Catalog indices need not follow the polygon boundary. Find a stable
      // plane before sorting, including when the first three are collinear.
      Point raw{};
      for (std::size_t i = 1; i < face.size(); ++i)
        for (std::size_t j = i + 1; j < face.size(); ++j) {
          const auto candidate = cross(sub(model.vertices[face[i]], model.vertices[face[0]]),
                                       sub(model.vertices[face[j]], model.vertices[face[0]]));
          if (dot(candidate, candidate) > dot(raw, raw))
            raw = candidate;
        }
      auto n = unit(raw);
      if (dot(n, sub(anchor, center)) < 0)
        n = scaled(n, -1);
      for (auto v : face)
        ensure(std::abs(dot(n, sub(model.vertices[v], anchor))) < 1e-10, id + "/" + port,
               "Port vertices must be coplanar.");
      for (const auto &v : model.vertices)
        ensure(dot(n, sub(v, anchor)) < 1e-10, id + "/" + port,
               "Ports must lie on an exterior supporting face.");
      const auto r = unit(sub(model.vertices[face[0]], anchor)), u = cross(n, r);
      std::sort(face.begin(), face.end(), [&](Index a, Index b) {
        const auto da = sub(model.vertices[a], anchor), db = sub(model.vertices[b], anchor);
        return std::atan2(dot(da, u), dot(da, r)) < std::atan2(dot(db, u), dot(db, r));
      });
      for (std::size_t i = 0; i < face.size(); ++i)
        ensure(dot(cross(sub(model.vertices[face[(i + 1) % face.size()]], model.vertices[face[i]]),
                         sub(model.vertices[face[(i + 2) % face.size()]],
                             model.vertices[face[(i + 1) % face.size()]])),
                   n) > 1e-14,
               id + "/" + port, "Port vertices must form a strictly convex polygon.");
      anchor = add(inset(anchor), scaled(n, lift));
      model.port_centers[port] = anchor;
      model.port_normals[port] = n;
      if (!diagram_ && !catalog_spherical_)
        asset(id + "/port/" + port, [&](const auto &triangle) {
          std::vector<Point> points(face.size());
          for (std::size_t i = 0; i < face.size(); ++i)
            points[i] = add(anchor, scaled(sub(add(inset(model.vertices[face[i]]), scaled(n, lift)), anchor),
                                           1 - sticker_inset));
          for (std::size_t i = 1; i + 1 < points.size(); ++i)
            triangle(points[0], points[i], points[i + 1], n);
        });
    }
    for (const auto &data : data.at("symmetries")) {
      const auto symmetry = read_frame(data);
      for (const auto &v : model.vertices)
        ensure(std::any_of(model.vertices.begin(), model.vertices.end(),
                           [&](Point other) { return close(transformed(symmetry, v), other); }),
               id, "A declared symmetry must preserve the model vertices.");
      for (const auto &[port, anchor] : model.port_centers)
        ensure(close(transformed(symmetry, anchor), anchor) &&
                   close(transformed(symmetry, model.port_normals.at(port)), model.port_normals.at(port)),
               id, "A declared symmetry must preserve each labeled port.");
      model.symmetries.push_back(symmetry);
    }
    ensure(!model.symmetries.empty(), id, "Declare at least the identity model symmetry.");
    model_ids[id] = static_cast<Index>(models_.size());
    models_.push_back(std::move(model));
  }
  if (diagram_)
    asset("port-marker", [&](const auto &triangle) {
      triangle({-0.17, -0.14, 0}, {0.17, -0.14, 0}, {0, 0.17, 0}, {0, 0, 1});
    });
  std::size_t placements = 0;
  for (const auto &domain : definition_->domains) {
    const auto model_id = realization.at("modelByDomain").at(domain.id).get<std::string>();
    ensure(model_ids.contains(model_id), domain.id, "Domain refers to a missing geometric model.");
    model_of_domain_.push_back(model_ids.at(model_id));
    std::vector<Frame> frames;
    for (const auto &q : domain.placements) {
      frames.push_back(read_frame(realization.at("placementFrames").at(q.key)));
      ++placements;
      const auto &model = models_[model_ids.at(model_id)];
      ensure(model.port_centers.size() == q.ports.size(), q.key,
             "Model and abstract placement ports differ.");
      for (const auto &[port, _] : q.ports)
        ensure(model.port_centers.contains(port), q.key, "A bound port has no geometry.");
    }
    placement_frames_.push_back(std::move(frames));
  }
  ensure(realization.at("placementFrames").size() == placements, "/placementFrames",
         "Frame coverage must match the catalog.");
  ensure(realization.at("operationTracks").size() == definition_->operations.size(), "/operationTracks",
         "Every operation needs a geometric track.");
  const auto pi = std::numbers::pi;
  const auto stops = realization.at("angularStops").get<std::vector<double>>();
  ensure(
      stops.size() >= 2 && stops.size() <= 128 &&
          std::all_of(stops.begin(), stops.end(),
                      [&](double angle) { return std::isfinite(angle) && angle >= 0 && angle < 2 * pi; }) &&
          std::adjacent_find(stops.begin(), stops.end(), std::greater_equal<double>()) == stops.end(),
      "/angularStops", "Clockwise stops must be finite, distinct, and sorted within one revolution.");
  for (const auto &operation : definition_->operations) {
    const auto &track = realization.at("operationTracks").at(operation.id);
    const auto from = track.at("fromStop").get<int>(), to = track.at("toStop").get<int>();
    ensure(from >= 0 && static_cast<std::size_t>(from) < stops.size() && to >= 0 &&
               static_cast<std::size_t>(to) < stops.size(),
           operation.id, "Track stops must be in the angular catalog.");
    auto angle = std::remainder(stops[from] - stops[to], 2 * pi);
    if (std::abs(std::abs(angle) - pi) < 1e-12)
      angle = -pi;
    tracks_.push_back({unit(track.at("axis").get<Point>()), angle});
  }
  for (Index i = 0; i < definition_->operations.size(); ++i)
    validate_catalog_transport(i);
  if (catalog_spherical_)
    build_catalog_spherical(realization);
  for (Index i = 0; i < definition_->pieces.size(); ++i) {
    const auto &piece = definition_->pieces[i];
    const auto &model = models_[model_of_domain_[piece.domain]];
    auto add_part = [&](const std::string &port, const std::string &role) {
      const auto id = piece.id + "/" + role + "/" + (port.empty() ? "main" : port);
      parts_.push_back({i, port, role, "", id});
      Json binding{{"visualPartId", id},
                   {"pieceId", piece.id},
                   {"role", role},
                   {"meshAssetId", role == "body" ? model.id + "/body"
                                   : diagram_     ? "port-marker"
                                                  : model.id + "/port/" + port},
                   {"materialBindingId", role == "port"         ? piece.labels.at(port)
                                         : piece.labels.empty() ? "mechanism"
                                                                : "body"}};
      if (!port.empty()) {
        binding["portId"] = port;
        if (diagram_) {
          binding["label"] = piece.id.substr(piece.id.find('/') + 1) + " · " + port;
          binding["labelScale"] = 0.32;
        }
      }
      scene_["visualParts"].push_back(binding);
    };
    if (!diagram_)
      add_part("", "body");
    for (const auto &[port, _] : piece.labels)
      add_part(port, "port");
  }
  scene_["fidelity"] = realization.value("fidelity", "idealized-polyhedral");
  scene_["catalogTransportVerified"] = true;
}

Geometry::Frame Geometry::catalog_resting(const Part &part, const State &state) const {
  const auto &piece = definition_->pieces[part.piece];
  auto frame = placement_frames_[piece.domain][state.placement_of[part.piece]];
  if (!diagram_) {
    for (int col = 0; col < 3; ++col)
      for (int row = 0; row < 3; ++row)
        frame[col * 4 + row] *= scale_;
    return frame;
  }
  const auto &model = models_[model_of_domain_[piece.domain]];
  const auto n = transformed(frame, model.port_normals.at(part.port));
  const auto p = transformed(frame, model.port_centers.at(part.port));
  const std::array<Point, 6> normals{{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}, {0, 0, -1}}};
  const std::array<Point, 6> rights{{{1, 0, 0}, {0, 0, -1}, {1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}}};
  const std::array<Point, 6> centers{{{0, 3, 0}, {3, 0, 0}, {0, 0, 0}, {0, -3, 0}, {-3, 0, 0}, {6, 0, 0}}};
  int face = 0;
  for (int i = 1; i < 6; ++i)
    if (dot(n, normals[i]) > dot(n, normals[face]) + 1e-12)
      face = i;
  auto result = identity();
  result[12] = centers[face][0] + dot(p, rights[face]);
  result[13] = centers[face][1] + dot(p, cross(normals[face], rights[face]));
  return result;
}

void Geometry::validate_catalog_transport(Index operation_index) const {
  const auto &operation = definition_->operations[operation_index];
  const auto &track = tracks_[operation_index];
  for (Index domain = 0; domain < definition_->domains.size(); ++domain) {
    const auto &model = models_[model_of_domain_[domain]];
    for (Index q = 0; q < operation.maps[domain].size(); ++q) {
      if (operation.roles[domain][q] != 1)
        continue;
      const auto &before = placement_frames_[domain][q];
      const auto &after = placement_frames_[domain][operation.maps[domain][q]];
      const auto endpoint = rotated(before, track.axis, track.angle);
      ensure(std::any_of(model.symmetries.begin(), model.symmetries.end(),
                         [&](const Frame &symmetry) { return close(endpoint, composed(after, symmetry)); }),
             operation.id, "Geometric endpoint disagrees with the exact placement transport.");
      for (const auto &[port, anchor] : model.port_centers) {
        ensure(close(scaled(transformed(endpoint, anchor), scale_),
                     scaled(transformed(after, anchor), scale_)) &&
                   close(transformed(endpoint, model.port_normals.at(port)),
                         transformed(after, model.port_normals.at(port))),
               operation.id, "Geometric track disagrees with labeled port transport.");
      }
    }
  }
}
} // namespace twisty
