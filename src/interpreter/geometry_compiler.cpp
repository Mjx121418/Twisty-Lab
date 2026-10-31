#include "geometry_compiler.hpp"
#include "backend_contract.hpp"
#include "twisty/controller.hpp"
#include "../artifact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <tuple>

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
std::string paired_bandage_port(const std::string &port) {
  if (port == "0")
    return "2";
  if (port == "2")
    return "0";
  if (port == "1")
    return "4";
  if (port == "4")
    return "1";
  return {};
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
GeometryCompiler::GeometryCompiler(std::shared_ptr<const Execution> execution,
                                   const std::string &realization_json)
    : GeometryCompiler(std::move(execution), Json::parse(realization_json)) {}
GeometryCompiler::GeometryCompiler(std::shared_ptr<const Execution> execution,
                                   const Json &realization, const std::string &payload_digest)
    : execution_(std::move(execution)), definition_(execution_->definition()) {
  validate_motion_families(realization);
  if (realization.at("schemaVersion") != 1 ||
      realization.at("compatibleDefinitionDigest") != definition_->digest)
    throw DiagnosticError("realization.digest_mismatch", "/compatibleDefinitionDigest",
                          "Realization is not compatible with the exact definition.");
  const auto kind = realization.at("kind").get<std::string>();
  const auto &backend = detail::geometry_backend(kind);
  detail::validate_backend_capabilities(backend, realization.at("requiredCapabilities"));
  if (backend.engine != detail::GeometryEngine::spatial)
    throw DiagnosticError("realization.unsupported", "/kind", "This backend requires another geometry compiler.");
  diagram_ = backend.diagram;
  catalog_ = backend.polyhedral;
  spherical_ = backend.cube && backend.spherical;
  catalog_spherical_ = backend.polyhedral && backend.spherical;
  flat_surface_ = backend.flat;
  articulated_ = backend.articulated;
  if (realization.contains("explicitCubeTracks")) {
    const auto capabilities = realization.at("requiredCapabilities").get<std::vector<std::string>>();
    if (std::find(capabilities.begin(), capabilities.end(), "explicit-cube-tracks") == capabilities.end())
      throw DiagnosticError("realization.capability", "/requiredCapabilities", "Explicit cube tracks require their capability declaration.");
    if (!backend.cube || !realization.at("explicitCubeTracks").is_object() ||
        realization.at("explicitCubeTracks").size() != definition_->operations.size())
      throw DiagnosticError("realization.track", "/explicitCubeTracks", "Explicit cube tracks must cover every operation.");
    explicit_cube_tracks_ = true;
    for (const auto &op : definition_->operations) {
      const auto &track = realization.at("explicitCubeTracks").at(op.id);
      artifact::detail::fields(track, {"axis", "angle"});
      if (!track.at("axis").is_array() || track.at("axis").size() != 3 || !track.at("angle").is_number())
        throw DiagnosticError("realization.track", "/explicitCubeTracks", "Expected an axis and signed angle.");
      Point axis = track.at("axis").get<Point>(); double norm = 0;
      for (double x : axis) { if (!std::isfinite(x) || std::abs(x) > 100) throw DiagnosticError("realization.track", "/axis", "Invalid rotation axis."); norm += x*x; }
      const auto angle = track.at("angle").get<double>();
      if (norm < 1e-20 || !std::isfinite(angle) || std::abs(angle) > 100)
        throw DiagnosticError("realization.track", "/angle", "Invalid signed rotation.");
      for (double &x : axis) x /= std::sqrt(norm);
      tracks_.push_back({axis, angle});
    }
  }
  if (execution_->articulated_events() && !articulated_)
    throw DiagnosticError("realization.unsupported", "/kind",
                          "This binding catalog requires articulated event tracks.");
  if (!catalog_ && !articulated_) {
    for (const auto &piece : definition_->pieces)
      for (const auto &[_, label] : piece.labels)
        normal(label);
    if (!explicit_cube_tracks_) for (const auto &operation : definition_->operations) normal(operation.family);
  }
  scene_ = {{"sceneId", realization.at("id").get<std::string>() + ":" + definition_->digest},
            {"realizationId", realization.at("id")},
            {"definitionDigest", definition_->digest},
            {"requiredCapabilities", realization.at("requiredCapabilities")},
            {"meshAssets", Json::array()},
            {"visualParts", Json::array()},
            {"labels", Json::array()},
            {"diagram", diagram_}};
  if (execution_->source_free()) {
    scene_["executionDigest"] = definition_->digest;
    scene_["interpretationDigest"] = execution_->interpretation_digest();
    scene_["realizationDigest"] = payload_digest;
    scene_["sceneId"] = realization.at("id").get<std::string>() + ":" + payload_digest;
  }
  if(realization.contains("controlLayout")) {
    const auto ensure=[](bool ok,const std::string &source,const std::string &message) {
      if(!ok) throw DiagnosticError("realization.control_layout",source,message);
    };
    const auto &layout=realization.at("controlLayout");
    ensure(layout.at("version")==1 && layout.at("drivers").is_object(), "/controlLayout", "Invalid control layout.");
    const auto controls=execution_->controls();
    ensure(controls && layout.at("drivers").size()==controls->json().at("drivers").size(),
      "/controlLayout", "Control layout does not match the declared drivers.");
    for(const auto &d:controls->json().at("drivers")) {
      const auto &angles=layout.at("drivers").at(d.at("id").get<std::string>()).at("angles");
      ensure(angles.is_array() && angles.size()==d.at("stops").size(), "/controlLayout", "Stop angles differ in arity.");
      for(const auto &a:angles) ensure(a.is_number() && std::isfinite(a.get<double>()) && a>=0 && a<2*std::numbers::pi,
        "/controlLayout", "Stop angles must lie in one revolution.");
    }
    if(layout.contains("paths")) for(const auto &[path,angles]:layout.at("paths").items()) {
      ensure(!path.empty() && angles.is_object(), "/controlLayout/paths", "Invalid display path.");
      for(const auto &[d,a]:angles.items()) ensure(layout.at("drivers").contains(d) && a.is_number() &&
        std::isfinite(a.get<double>()) && std::abs(a.get<double>())<=100, "/controlLayout/paths", "Invalid display path angle.");
    }
    scene_["controlLayout"]=layout;
  }
  if (articulated_) {
    if (flat_surface_)
      build_flat_surface(realization);
    else
      build_articulated(realization);
    return;
  }
  if (catalog_) {
    build_catalog(realization);
    return;
  }
  if (spherical_) {
    build_spherical(realization);
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
  const bool compound = !diagram_ && std::any_of(definition_->pieces.begin(), definition_->pieces.end(),
                                                 [](const auto &piece) { return piece.type == "bandage"; });
  if (compound) {
    const auto position_offset = positions_.size(), index_offset = indices_.size();
    // One rigid cuboid spanning two neighboring cells, with the usual .03
    // clearance. Reuse the cube's flat face normals and local triangle pattern.
    for (std::size_t i = 0; i < 72; ++i) {
      positions_.push_back(positions_[i] * (i % 3 == 0 ? 0.97f / 0.47f : 1.f));
      normals_.push_back(normals_[i]);
    }
    for (std::size_t i = 0; i < 36; ++i)
      indices_.push_back(indices_[i]);
    scene_["meshAssets"].push_back({{"id", "bandage/body"},
                                    {"positionOffset", position_offset},
                                    {"vertexCount", 24},
                                    {"indexOffset", index_offset},
                                    {"indexCount", 36}});
    const auto port_offset = positions_.size(), port_indices = indices_.size();
    for (const auto &point : {V{-.45, -.4, 0}, V{.45, -.4, 0}, V{.45, .4, 0}, V{-.45, .4, 0}}) {
      positions_.insert(positions_.end(), {static_cast<float>(point.x), static_cast<float>(point.y), 0});
      normals_.insert(normals_.end(), {0, 0, 1});
    }
    indices_.insert(indices_.end(), {0, 1, 2, 0, 2, 3});
    scene_["meshAssets"].push_back({{"id", "bandage/port"},
                                    {"positionOffset", port_offset},
                                    {"vertexCount", 4},
                                    {"indexOffset", port_indices},
                                    {"indexCount", 6}});
  }
  auto add_part = [&](Index index, const std::string &port, const std::string &role,
                      const std::string &component) {
    const auto &piece = definition_->pieces[index];
    const auto id = piece.id + "/" + role + "/" + (role == "port" ? port : component);
    parts_.push_back({index, port, role, component, id});
    const bool joined_port =
        compound && piece.type == "bandage" && role == "port" && !paired_bandage_port(port).empty();
    Json binding{{"visualPartId", id},
                 {"pieceId", piece.id},
                 {"meshAssetId", joined_port            ? "bandage/port"
                                 : component == "fused" ? "bandage/body"
                                 : role == "port"       ? "port"
                                                        : "body"},
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
      add_part(i, "", "body", piece.type == "bandage" ? "fused" : "0");
    }
    for (const auto &[port, _] : piece.labels)
      add_part(i, port, "port", "");
  }
}
std::array<double, 16> GeometryCompiler::resting(const Part &part, const State &state) const {
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
    const auto paired = piece.type == "bandage" ? paired_bandage_port(part.port) : std::string{};
    if (!paired.empty()) {
      const auto other = cell_position(placement.ports.at(paired).first);
      const auto along = other + p * -1;
      return matrix(along, cross(n, along), n, p + along * .05 + n * .478);
    }
    return matrix(r, u, n, p + n * 0.478);
  }
  if (part.component == "fused") {
    const auto p = cell_position(placement.ports.at("0").first);
    const auto other = cell_position(placement.ports.at("2").first);
    const auto x = other + p * -1, y = normal(placement.ports.at("0").second);
    return matrix(x, y, cross(x, y), (p + other) * .5);
  }
  const auto p = cell_position(placement.ports.at(part.component).first);
  return matrix({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, p);
}
} // namespace twisty

namespace twisty {
std::shared_ptr<const GeometryProgram> GeometryCompiler::compile() {
  auto program = std::make_shared<GeometryProgram>();
  program->execution = execution_;
  // These legacy authoring formats use three-coordinate embedding meshes.
  // Space semantics remain explicit, independent of the renderer's camera.
  const auto space = scene_.value("ambientSpace", std::string{"R3"});
  const bool sphere = space == "S2";
  const bool plane = space == "E2";
  const auto radius = scene_.value("sphereRadius", 1.0);
  program->space = {{"id", space},
                    {"intrinsicDimension", sphere || plane ? 2 : 3},
                    {"coordinateModel", sphere ? "sphere-in-euclidean-3" : "euclidean-homogeneous-3"},
                    {"metric", "euclidean"},
                    {"curvature", sphere ? 1 / (radius * radius) : 0},
                    {"pointCoordinates", 3},
                    {"embeddingDimension", 3},
                    {"metricSignature", {1, 1, 1}},
                    {"isometryRows", 4},
                    {"isometryColumns", 4},
                    {"isometryScalar", "float64"},
                    {"matrixOrder", "column-major"},
                    {"homogeneous", true},
                    {"meshCoordinates", sphere ? "radial-display-shells" : "cartesian"}};
  if (sphere)
    program->space["radius"] = radius;
  program->scene = std::move(scene_);
  program->scene["space"] = program->space;
  program->scene["projections"] = {"embedding"};
  program->scene["drawingSpace"] = "R3";
  program->scene["drawingBufferVersion"] = 1;
  program->positions = std::move(positions_);
  program->normals = std::move(normals_);
  program->indices = std::move(indices_);
  program->minimum_motion_extent = diagram_ && articulated_ ? std::numbers::pi / 2 : 0;

  // Certify cube transport during compilation, as the authored catalog loaders
  // already do. The evaluator needs no cube attachments or backend switches.
  if (!catalog_ && !articulated_)
    for (const auto &op : definition_->operations) {
      const auto index = definition_->operation_ids.at(op.id);
      const auto axis = explicit_cube_tracks_ ? V{tracks_[index].axis[0], tracks_[index].axis[1], tracks_[index].axis[2]} : normal(op.family);
      const double angle = explicit_cube_tracks_ ? tracks_[index].angle : (op.transport.ends_with(".counterclockwise") ? 1 : -1) * std::numbers::pi / 2;
      for (Index d = 0; d < definition_->domains.size(); ++d) {
        const auto &domain = definition_->domains[d];
        for (Index q = 0; q < domain.placements.size(); ++q) {
          if (op.roles[d][q] != 1)
            continue;
          const auto &source = domain.placements[q];
          const auto &target = domain.placements[op.maps[d][q]];
          for (const auto &[port, attachment] : source.ports) {
            const auto &destination = target.ports.at(port);
            const auto dn = rotate(normal(attachment.second), axis, angle) + normal(destination.second) * -1;
            const auto dp =
                rotate(cell_position(attachment.first), axis, angle) + cell_position(destination.first) * -1;
            if (dot(dn, dn) > 1e-12 || dot(dp, dp) > 1e-12)
              throw DiagnosticError("realization.unsupported_transition", op.id,
                                    "Geometric track does not agree with the abstract port transport.");
          }
        }
      }
    }

  using TableKey = std::tuple<Index, std::string, std::string, std::string, Index>;
  std::map<TableKey, Index> tables;
  for (const auto &part : parts_) {
    const auto domain = definition_->pieces[part.piece].domain;
    const TableKey key = articulated_ || spherical_
                             ? TableKey{std::numeric_limits<Index>::max(), "", "", "", part.frame_index}
                         : catalog_ && !diagram_ ? TableKey{domain, "physical", "", "", 0}
                                                 : TableKey{domain, part.kind, part.port, part.component, 0};
    auto [entry, added] = tables.emplace(key, static_cast<Index>(program->tables.size()));
    if (added) {
      GeometryPoseTable table;
      table.linear_translation = diagram_;
      if (articulated_) {
        auto &component = components_[part.frame_index];
        table.frames = std::move(component.frames);
        table.tracks = std::move(component.tracks);
      } else {
        auto state = definition_->initial;
        for (Index q = 0; q < definition_->domains[domain].placements.size(); ++q) {
          state.placement_of[part.piece] = q;
          auto pose = resting(part, state);
          if (catalog_ && !diagram_)
            for (int col = 0; col < 3; ++col)
              for (int row = 0; row < 3; ++row)
                pose[col * 4 + row] /= scale_;
          table.frames.push_back(pose);
        }
        for (Index op = 0; op < definition_->operations.size(); ++op) {
          GeometryTrack track;
          const auto &operation = definition_->operations[op];
          const auto n = catalog_ || explicit_cube_tracks_ ? V{tracks_[op].axis[0], tracks_[op].axis[1], tracks_[op].axis[2]}
                                  : normal(operation.family);
          const auto angle =
              catalog_ || explicit_cube_tracks_ ? tracks_[op].angle
                       : (operation.transport.ends_with(".counterclockwise") ? 1 : -1) * std::numbers::pi / 2;
          track.extent_hint = std::abs(angle);
          if (!diagram_)
            track.uniform.push_back({{n.x, n.y, n.z}, {0, 0, 0}, angle, false});
          table.tracks.push_back(std::move(track));
        }
      }
      program->tables.push_back(std::move(table));
    }
    program->part_ids.emplace(part.id, static_cast<Index>(program->parts.size()));
    if (catalog_ && !diagram_ && scale_ != 1)
      program->scene["visualParts"][program->parts.size()]["meshScale"] = scale_;
    program->parts.push_back({part.piece, entry->second, catalog_ && !diagram_ ? scale_ : 1});
  }
  return program;
}
} // namespace twisty
