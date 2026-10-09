#include "twisty/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace twisty {
namespace {
struct V {
  double x, y, z;
  V operator+(V b) const {
    return {x + b.x, y + b.y, z + b.z};
  }
  V operator*(double s) const {
    return {x * s, y * s, z * s};
  }
};
double dot(V a, V b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
V cross(V a, V b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V normal(const std::string &face) {
  if (face == "U")
    return {0, 1, 0};
  if (face == "D")
    return {0, -1, 0};
  if (face == "R")
    return {1, 0, 0};
  if (face == "L")
    return {-1, 0, 0};
  if (face == "F")
    return {0, 0, 1};
  if (face == "B")
    return {0, 0, -1};
  throw DiagnosticError("realization.attachment", face, "This realization requires cube face attachments.");
}
V right(const std::string &face) {
  if (face == "R")
    return {0, 0, -1};
  if (face == "L")
    return {0, 0, 1};
  if (face == "B")
    return {-1, 0, 0};
  return {1, 0, 0};
}
V cell_position(const std::string &cell) {
  V p{0, 0, 0};
  for (const char face : cell)
    p = p + normal(std::string(1, face));
  return p;
}
V rotate(V value, V axis, double angle) {
  return value * std::cos(angle) + cross(axis, value) * std::sin(angle) +
         axis * (dot(axis, value) * (1 - std::cos(angle)));
}
std::array<double, 16> matrix(V x, V y, V z, V p) {
  return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
}
std::array<double, 16> rotated(std::array<double, 16> m, V axis, double angle) {
  for (int column = 0; column < 4; ++column) {
    const auto v = rotate({m[column * 4], m[column * 4 + 1], m[column * 4 + 2]}, axis, angle);
    m[column * 4] = v.x;
    m[column * 4 + 1] = v.y;
    m[column * 4 + 2] = v.z;
  }
  return m;
}
V diagram_center(const std::string &face) {
  if (face == "U")
    return {0, 3, 0};
  if (face == "D")
    return {0, -3, 0};
  if (face == "L")
    return {-3, 0, 0};
  if (face == "R")
    return {3, 0, 0};
  if (face == "B")
    return {6, 0, 0};
  return {0, 0, 0};
}
} // namespace
Geometry::Geometry(const std::string &definition_json, const std::string &realization_json)
    : Geometry(load_definition(Json::parse(definition_json)), realization_json) {}
Geometry::Geometry(std::shared_ptr<const Definition> definition, const std::string &realization_json)
    : definition_(std::move(definition)), displayed_(definition_->initial), before_(displayed_),
      after_(displayed_) {
  const auto realization = Json::parse(realization_json);
  if (realization.at("schemaVersion") != 1 ||
      realization.at("compatibleDefinitionDigest") != definition_->digest)
    throw DiagnosticError("realization.digest_mismatch", "/compatibleDefinitionDigest",
                          "Realization is not compatible with the exact definition.");
  const auto kind = realization.at("kind").get<std::string>();
  if (kind != "cube-euclidean" && kind != "cube-port-diagram" && kind != "cube-spherical" &&
      kind != "polyhedral-euclidean" && kind != "polyhedral-port-diagram" && kind != "polyhedral-spherical")
    throw DiagnosticError("realization.unsupported", "/kind", "Unknown realization kind.");
  diagram_ = kind.ends_with("port-diagram");
  catalog_ = kind.starts_with("polyhedral-");
  spherical_ = kind == "cube-spherical";
  catalog_spherical_ = kind == "polyhedral-spherical";
  if (!catalog_) {
    for (const auto &piece : definition_->pieces)
      for (const auto &[_, label] : piece.labels)
        normal(label);
    for (const auto &operation : definition_->operations)
      normal(operation.family);
  }
  scene_ = {{"sceneId", realization.at("id").get<std::string>() + ":" + definition_->digest},
            {"realizationId", realization.at("id")},
            {"definitionDigest", definition_->digest},
            {"requiredCapabilities", realization.at("requiredCapabilities")},
            {"meshAssets", Json::array()},
            {"visualParts", Json::array()},
            {"labels", Json::array()},
            {"diagram", diagram_}};
  if (catalog_) {
    build_catalog(realization);
    moving_.resize(definition_->pieces.size(), false);
    transforms_.resize(parts_.size() * 16);
    sample(0);
    return;
  }
  if (spherical_) {
    build_spherical(realization);
    moving_.resize(definition_->pieces.size(), false);
    transforms_.resize(parts_.size() * 16);
    sample(0);
    return;
  }
  auto face_mesh = [&](V n, V r, V u, double radius, bool body) {
    const auto start = static_cast<std::uint32_t>(positions_.size() / 3);
    const V center = body ? n * radius : V{0, 0, 0};
    for (const auto &v : {center + r * (-radius) + u * (-radius), center + r * radius + u * (-radius),
                          center + r * radius + u * radius, center + r * (-radius) + u * radius}) {
      positions_.insert(positions_.end(),
                        {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)});
      normals_.insert(normals_.end(),
                      {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
    }
    const auto local = body ? start : 0;
    indices_.insert(indices_.end(), {local, local + 1, local + 2, local, local + 2, local + 3});
  };
  for (const auto &face : {"U", "R", "F", "D", "L", "B"}) {
    const auto n = normal(face), r = right(face);
    face_mesh(n, r, cross(n, r), 0.47, true);
  }
  scene_["meshAssets"].push_back(
      {{"id", "body"}, {"positionOffset", 0}, {"vertexCount", 24}, {"indexOffset", 0}, {"indexCount", 36}});
  face_mesh({0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0.40, false);
  scene_["meshAssets"].push_back(
      {{"id", "port"}, {"positionOffset", 72}, {"vertexCount", 4}, {"indexOffset", 36}, {"indexCount", 6}});
  auto add_part = [&](Index index, const std::string &port, const std::string &role,
                      const std::string &component) {
    const auto &piece = definition_->pieces[index];
    const auto id = piece.id + "/" + role + "/" + (role == "port" ? port : component);
    parts_.push_back({index, port, role, component, id});
    Json binding{{"visualPartId", id},
                 {"pieceId", piece.id},
                 {"meshAssetId", role == "port" ? "port" : "body"},
                 {"materialBindingId", role == "port" ? piece.labels.at(port) : "body"},
                 {"role", role}};
    if (role == "port") {
      binding["portId"] = port;
      if (diagram_)
        binding["label"] = piece.id.substr(piece.id.find('/') + 1) + " · " + port;
    }
    scene_["visualParts"].push_back(binding);
  };
  for (Index i = 0; i < definition_->pieces.size(); ++i) {
    const auto &piece = definition_->pieces[i];
    if (!diagram_) {
      add_part(i, "", "body", "0");
      if (piece.type == "bandage") {
        add_part(i, "", "body", "2");
        add_part(i, "", "bridge", "0");
      }
    }
    for (const auto &[port, _] : piece.labels)
      add_part(i, port, "port", "");
  }
  moving_.resize(definition_->pieces.size(), false);
  transforms_.resize(parts_.size() * 16);
  sample(0);
}
std::array<double, 16> Geometry::resting(const Part &part, const State &state) const {
  if (spherical_)
    return spherical_frames_[part.frame_index][state.placement_of[part.piece]];
  if (catalog_)
    return catalog_resting(part, state);
  const auto &piece = definition_->pieces[part.piece];
  const auto &placement = definition_->domains[piece.domain].placements.at(state.placement_of.at(part.piece));
  if (part.kind == "port") {
    const auto &[cell, face] = placement.ports.at(part.port);
    const auto n = normal(face), r = right(face), u = cross(n, r), p = cell_position(cell);
    if (diagram_)
      return matrix({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, diagram_center(face) + V{dot(p, r), dot(p, u), 0});
    return matrix(r, u, n, p + n * 0.478);
  }
  auto p = cell_position(placement.ports.at(part.component).first);
  if (part.kind == "bridge") {
    const auto other = cell_position(placement.ports.at("2").first);
    const V delta{std::abs(p.x - other.x), std::abs(p.y - other.y), std::abs(p.z - other.z)};
    return matrix({delta.x > 0 ? 0.14 : 0.36, 0, 0}, {0, delta.y > 0 ? 0.14 : 0.36, 0},
                  {0, 0, delta.z > 0 ? 0.14 : 0.36}, (p + other) * 0.5);
  }
  return matrix({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, p);
}
void Geometry::set_state_json(const std::string &state) {
  const auto decoded = decode_state(*definition_, Json::parse(state));
  displayed_ = decoded;
  before_ = decoded;
  after_ = decoded;
  animated_ = false;
  sample(0);
}
std::string Geometry::prepare_animation_json(const std::string &input) {
  try {
    const auto transition = Json::parse(input);
    if (transition.at("definitionDigest") != definition_->digest)
      throw DiagnosticError("realization.digest_mismatch", "/transition",
                            "Transition uses a different definition.");
    auto before = decode_state(*definition_, transition.at("beforeState"));
    auto after = decode_state(*definition_, transition.at("afterState"));
    const auto operation_id = transition.at("request").at("operation").get<std::string>();
    const auto planned = plan(*definition_, before, operation_id);
    if (!planned.transition || planned.transition->after != after ||
        encode_transition(*definition_, *planned.transition) != transition)
      throw DiagnosticError("realization.witness", "/transition",
                            "Transition is not the recorded abstract witness.");
    const auto &operation = definition_->operations.at(definition_->operation_ids.at(operation_id));
    const auto op_index = definition_->operation_ids.at(operation_id);
    const auto track_axis = catalog_ ? tracks_[op_index].axis : Point{};
    const auto axis = catalog_ ? V{track_axis[0], track_axis[1], track_axis[2]} : normal(operation.family);
    const auto angle =
        catalog_ ? tracks_[op_index].angle
                 : (operation.transport.ends_with(".counterclockwise") ? 1 : -1) * std::numbers::pi / 2;
    std::vector<bool> moving(definition_->pieces.size(), false);
    for (const auto &action : planned.transition->actions) {
      moving[action.piece] = true;
      if (catalog_)
        continue; // All catalog transports and ports were checked when loading this realization.
      const auto &piece = definition_->pieces[action.piece];
      const auto &domain = definition_->domains[piece.domain];
      const auto &source = domain.placements[action.from];
      const auto &target = domain.placements[action.to];
      for (const auto &[port, attachment] : source.ports) {
        const auto &destination = target.ports.at(port);
        const auto transported_normal = rotate(normal(attachment.second), axis, angle);
        const auto transported_position = rotate(cell_position(attachment.first), axis, angle);
        const auto expected_normal = normal(destination.second),
                   expected_position = cell_position(destination.first);
        const V dn{transported_normal.x - expected_normal.x, transported_normal.y - expected_normal.y,
                   transported_normal.z - expected_normal.z};
        const V dp{transported_position.x - expected_position.x, transported_position.y - expected_position.y,
                   transported_position.z - expected_position.z};
        if (dot(dn, dn) > 1e-12 || dot(dp, dp) > 1e-12)
          throw DiagnosticError("realization.unsupported_transition", operation_id,
                                "Geometric track does not agree with the abstract port transport.");
      }
    }
    before_ = std::move(before);
    after_ = std::move(after);
    rotation_axis_ = {axis.x, axis.y, axis.z};
    rotation_angle_ = angle;
    moving_ = std::move(moving);
    animated_ = true;
    sample(0);
    return Json{{"status", "Prepared"}, {"durationHint", 220 * std::abs(angle) / (std::numbers::pi / 2)}}
        .dump();
  } catch (const std::exception &error) {
    auto result = diagnostic_result(error);
    result["status"] = "UnsupportedTransition";
    return result.dump();
  }
}
void Geometry::sample(double progress) {
  if (!std::isfinite(progress))
    throw DiagnosticError("animation.progress", "/progress", "Animation progress must be finite.");
  const double t = std::clamp(progress, 0.0, 1.0);
  const double eased = t * t * (3 - 2 * t);
  for (std::size_t i = 0; i < parts_.size(); ++i) {
    const auto &part = parts_[i];
    auto transform = resting(part, animated_ && t == 1 ? after_ : before_);
    if (animated_ && t > 0 && t < 1 && moving_[part.piece]) {
      if (diagram_) {
        const auto target = resting(part, after_);
        for (int coordinate = 12; coordinate < 15; ++coordinate)
          transform[coordinate] += (target[coordinate] - transform[coordinate]) * eased;
      } else
        transform = rotated(transform, {rotation_axis_[0], rotation_axis_[1], rotation_axis_[2]},
                            rotation_angle_ * eased);
    }
    for (std::size_t j = 0; j < 16; ++j)
      transforms_[i * 16 + j] = static_cast<float>(transform[j]);
  }
  if (animated_ && t == 1)
    displayed_ = after_;
  else
    displayed_ = before_;
}
std::string Geometry::bind_hit_json(const std::string &id) const {
  const auto found =
      std::find_if(parts_.begin(), parts_.end(), [&](const Part &part) { return part.id == id; });
  if (found == parts_.end())
    return Json{{"status", "NoTarget"}}.dump();
  const auto &piece = definition_->pieces[found->piece];
  const auto &placement =
      definition_->domains[piece.domain].placements[displayed_.placement_of[found->piece]];
  Json candidates = Json::array();
  for (const auto &operation : definition_->operations)
    if ((catalog_ && operation.roles[piece.domain][displayed_.placement_of[found->piece]] == 1) ||
        (!catalog_ &&
         std::any_of(placement.footprint.begin(), placement.footprint.end(), [&](const std::string &cell) {
           return std::find(operation.cells.begin(), operation.cells.end(), cell) != operation.cells.end();
         })))
      candidates.push_back({{"operation", operation.id}, {"parameters", Json::object()}});
  Json target{{"status", "Target"}, {"pieceId", piece.id}, {"operationCandidates", candidates}};
  if (!found->port.empty())
    target["portId"] = found->port;
  return target.dump();
}
} // namespace twisty
