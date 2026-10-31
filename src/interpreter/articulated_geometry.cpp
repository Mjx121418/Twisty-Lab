#include "geometry_compiler.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace twisty {
namespace {
using Point = std::array<double, 3>;
using Frame = std::array<double, 16>;
// Reserve Float32 headroom for all eight composed pivot rotations.
constexpr double coordinate_limit = std::numeric_limits<float>::max() / 128.0;
void ensure(bool ok, const std::string &source, const std::string &message) {
  if (!ok)
    throw DiagnosticError("realization.articulated", source, message);
}
Point add(Point a, Point b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Point scale(Point a, double s) {
  return {a[0] * s, a[1] * s, a[2] * s};
}
double dot(Point a, Point b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Point cross(Point a, Point b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Point point(const Json &j) {
  ensure(j.is_array() && j.size() == 3, "/point", "Expected a finite numeric triple.");
  auto p = j.get<Point>();
  for (double v : p)
    ensure(std::isfinite(v) && std::abs(v) <= coordinate_limit, "/point",
           "Coordinates exceed the Float32-safe range.");
  return p;
}
Point column(const Frame &m, int col) {
  return {m[col * 4], m[col * 4 + 1], m[col * 4 + 2]};
}
Point transform(const Frame &m, Point p) {
  auto out = column(m, 3);
  for (int i = 0; i < 3; ++i)
    out = add(out, scale(column(m, i), p[i]));
  return out;
}
Frame identity() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
Frame multiply(const Frame &a, const Frame &b) {
  Frame out{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
  return out;
}
Frame frame(const Json &j) {
  ensure(j.is_array() && j.size() == 16, "/frames", "Expected a column-major rigid 4x4 frame.");
  auto m = j.get<Frame>();
  for (double v : m)
    ensure(std::isfinite(v) && std::abs(v) <= coordinate_limit, "/frames",
           "Frame entries exceed the Float32-safe range.");
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 3; ++k)
      ensure(std::abs(dot(column(m, i), column(m, k)) - (i == k ? 1 : 0)) < 1e-9, "/frames",
             "Frame must be orthonormal.");
  ensure(dot(column(m, 0), cross(column(m, 1), column(m, 2))) > 1 - 1e-9 &&
             std::abs(m[3]) + std::abs(m[7]) + std::abs(m[11]) + std::abs(m[15] - 1) < 1e-9,
         "/frames", "Frame must be a proper rigid transform.");
  return m;
}
bool close(const Frame &a, const Frame &b) {
  for (int i = 0; i < 16; ++i)
    if (std::abs(a[i] - b[i]) > 1e-6)
      return false;
  return true;
}
Frame rotation(Point n, Point pivot, double angle) {
  Frame out = identity();
  for (int col = 0; col < 3; ++col) {
    Point v{};
    v[col] = 1;
    v = add(add(scale(v, std::cos(angle)), scale(cross(n, v), std::sin(angle))),
            scale(n, dot(n, v) * (1 - std::cos(angle))));
    for (int row = 0; row < 3; ++row)
      out[col * 4 + row] = v[row];
  }
  const auto offset = add(pivot, scale(transform(out, pivot), -1));
  for (int row = 0; row < 3; ++row)
    out[12 + row] = offset[row];
  return out;
}
} // namespace

void GeometryCompiler::build_articulated(const Json &realization) {
  ensure(definition_->port_catalog, "/catalog/kind", "Articulated models require a port binding catalog.");
  const bool spherical = realization.at("kind") == "articulated-spherical";
  const double radius = spherical ? realization.at("radius").get<double>() : 0;
  const auto &capabilities = realization.at("requiredCapabilities");
  const bool shared_tracks =
      std::find(capabilities.begin(), capabilities.end(), "shared-rotation-tracks") != capabilities.end();
  const Json empty_library = Json::object();
  const auto &track_tables =
      realization.contains("operationTrackTables") ? realization.at("operationTrackTables") : empty_library;
  const auto &rotation_tracks =
      realization.contains("rotationTracks") ? realization.at("rotationTracks") : empty_library;
  ensure(track_tables.is_object() && rotation_tracks.is_object() && track_tables.size() <= 4096 &&
             rotation_tracks.size() <= 8192,
         "/operationTrackTables", "Shared track libraries exceed the realization limits.");
  ensure(shared_tracks || (track_tables.empty() && rotation_tracks.empty()), "/requiredCapabilities",
         "Named track libraries require shared-rotation-tracks.");
  ensure(!spherical || (std::isfinite(radius) && radius > 0 && radius <= 100), "/radius",
         "A spherical model requires a finite radius in (0,100].");
  struct Asset {
    std::vector<Point> vertices;
    struct Surface {
      Point normal;
      bool radial;
    };
    struct Edge {
      Point a, b, normal;
      bool radial;
    };
    std::vector<Edge> boundary;
  };
  std::map<std::string, Asset> assets;
  std::size_t total_vertices = 0, total_triangles = 0, total_frames = 0, total_steps = 0;
  auto path_library = std::make_shared<std::vector<std::vector<Rotation>>>(1);
  struct PreparedPath {
    Index index;
    std::vector<Frame> matrices;
  };
  std::map<std::string, PreparedPath> named_paths;
  const auto prepare_path = [&](const Json &input, const std::string &id) {
    Json generated;
    if(input.is_object() && input.contains("family")) {
      const auto r=family_rotation(realization,input);
      generated=Json::array({{{"axis",r.axis},{"pivot",r.pivot},{"angle",r.angle},{"space",r.local?"local":"world"}}});
    }
    const auto &steps=generated.is_null()?input:generated;
    ensure(steps.is_array() && steps.size() <= 8, id, "A track has at most eight simple rotations.");
    std::vector<Rotation> path;
    std::vector<Frame> matrices;
    for (const auto &step : steps) {
      auto axis = point(step.at("axis"));
      const auto length = std::sqrt(dot(axis, axis));
      ensure(length > 1e-12, id, "Rotation axis is degenerate.");
      axis = scale(axis, 1 / length);
      const auto pivot = point(step.at("pivot"));
      ensure(!flat_surface_ || (std::abs(axis[0]) + std::abs(axis[1]) < 1e-9 && pivot[2] == 0 &&
                                std::abs(pivot[0]) <= 2 * flat_side_ && std::abs(pivot[1]) <= 2 * flat_side_),
             id, "Flat-surface tracks require bounded planar rotations.");
      ensure(!spherical || (pivot[0] == 0 && pivot[1] == 0 && pivot[2] == 0), id,
             "Spherical animation pivots must be the sphere origin.");
      const auto angle = step.at("angle").get<double>();
      const auto space = step.at("space").get<std::string>();
      ensure(std::isfinite(angle) && std::abs(angle) <= 100 && (space == "local" || space == "world"), id,
             "Invalid rotation angle or coordinate space.");
      path.push_back({axis, pivot, angle, space == "local"});
      matrices.push_back(rotation(axis, pivot, angle));
    }
    Index index = 0;
    if (!path.empty()) {
      index = static_cast<Index>(path_library->size());
      path_library->push_back(std::move(path));
    }
    return PreparedPath{index, std::move(matrices)};
  };
  for (const auto &[id, data] : realization.at("assets").items()) {
    total_vertices += data.at("vertices").size();
    total_triangles += data.at("triangles").size();
    ensure(total_vertices <= 262144 && total_triangles <= 524288, id,
           "Combined meshes exceed the realization limits.");
    ensure(assets.size() < 256 && data.at("vertices").size() >= 3 && data.at("vertices").size() <= 65536 &&
               !data.at("triangles").empty() && data.at("triangles").size() <= 262144,
           id, "Mesh exceeds the articulated asset limits.");
    Asset asset;
    for (const auto &v : data.at("vertices")) {
      const auto p = point(v);
      ensure(!flat_surface_ || p[2] == 0, id, "Flat-surface assets must lie in their local z=0 plane.");
      ensure(!spherical || dot(p, p) <= (radius + 1e-6) * (radius + 1e-6), id,
             "A spherical mesh must fit within its declared radius.");
      asset.vertices.push_back(p);
    }
    const auto po = positions_.size(), io = indices_.size();
    std::vector<bool> radial_triangles(data.at("triangles").size(), false);
    const auto radial = data.value("radialNormalTriangles", Json::array());
    ensure(radial.is_array() && radial.size() <= radial_triangles.size(), id,
           "Radial normals require a list of distinct triangle indices.");
    for (const auto &entry : radial) {
      ensure(entry.is_number_integer() && entry.get<std::int64_t>() >= 0 &&
                 entry.get<std::uint64_t>() < radial_triangles.size(),
             id, "Radial normal triangle index is outside the asset.");
      const auto index = entry.get<Index>();
      ensure(!radial_triangles[index], id, "Radial normal triangle indices must be distinct.");
      radial_triangles[index] = true;
    }
    std::map<std::pair<Index, Index>, std::vector<Asset::Surface>> edges;
    std::size_t triangle_index = 0;
    for (const auto &triangle : data.at("triangles")) {
      const bool smooth = radial_triangles[triangle_index++];
      ensure(triangle.is_array() && triangle.size() == 3, id, "Expected triangle indices.");
      for (const auto &v : triangle)
        ensure(v.is_number_integer() && v.get<std::int64_t>() >= 0 && v.get<std::uint64_t>() <= UINT32_MAX,
               id, "Triangle indices must be nonnegative uint32 integers.");
      const auto tri = triangle.get<std::array<Index, 3>>();
      for (auto v : tri)
        ensure(v < asset.vertices.size(), id, "Triangle index is outside the asset.");
      auto normal = cross(add(asset.vertices[tri[1]], scale(asset.vertices[tri[0]], -1)),
                          add(asset.vertices[tri[2]], scale(asset.vertices[tri[0]], -1)));
      const auto length = std::sqrt(dot(normal, normal));
      ensure(length > 1e-12, id, "A triangle is degenerate.");
      normal = scale(normal, 1 / length);
      const auto center = add(add(asset.vertices[tri[0]], asset.vertices[tri[1]]), asset.vertices[tri[2]]);
      const double direction = dot(normal, center) < 0 ? -1 : 1;
      ensure(!smooth || std::abs(dot(normal, center)) > 1e-12, id,
             "A radial surface must have an inward or outward orientation.");
      // Coplanar internal triangulation edges cancel; creases and polygon
      // boundaries retain their directed edge and outward normal.
      for (int i = 0; i < 3; ++i) {
        const auto a = tri[i], b = tri[(i + 1) % 3];
        auto &opposite = edges[{b, a}];
        const auto found = std::find_if(opposite.begin(), opposite.end(), [&](const Asset::Surface &s) {
          return s.radial == smooth && dot(s.normal, normal) > 1 - 1e-10;
        });
        if (found != opposite.end())
          opposite.erase(found);
        else
          edges[{a, b}].push_back({normal, smooth});
      }
      for (auto v : tri) {
        indices_.push_back(static_cast<Index>((positions_.size() - po) / 3));
        for (double value : asset.vertices[v])
          positions_.push_back(static_cast<float>(value));
        auto shading_normal = normal;
        if (smooth) {
          const auto &p = asset.vertices[v];
          const auto radius = std::sqrt(dot(p, p));
          ensure(radius > 1e-12, id, "A radial normal requires a vertex away from the origin.");
          shading_normal = scale(p, direction / radius);
        }
        for (double value : shading_normal)
          normals_.push_back(static_cast<float>(value));
      }
    }
    for (const auto &[edge, normals] : edges)
      for (const auto &surface : normals)
        asset.boundary.push_back(
            {asset.vertices[edge.first], asset.vertices[edge.second], surface.normal, surface.radial});
    scene_["meshAssets"].push_back({{"id", id},
                                    {"positionOffset", po},
                                    {"vertexCount", (positions_.size() - po) / 3},
                                    {"indexOffset", io},
                                    {"indexCount", indices_.size() - io}});
    assets[id] = std::move(asset);
  }
  ensure(!assets.empty(), "/assets", "At least one mesh is required.");
  ensure(realization.at("componentsByDomain").size() == definition_->domains.size(), "/componentsByDomain",
         "Components must cover exactly the declared domains.");
  std::vector<std::vector<std::pair<Index, Json>>> bindings(definition_->domains.size());
  for (Index d = 0; d < definition_->domains.size(); ++d) {
    const auto &domain = definition_->domains[d];
    const auto &list = realization.at("componentsByDomain").at(domain.id);
    ensure(list.is_array() && !list.empty() && list.size() <= 32, domain.id, "Supply 1–32 rigid components.");
    std::set<std::string> ids, ports;
    for (const auto &data : list) {
      const auto id = data.at("id").get<std::string>(), asset_id = data.at("meshAssetId").get<std::string>();
      ensure(ids.insert(id).second && assets.contains(asset_id), id,
             "Component IDs must be unique and assets declared.");
      const auto port = data.value("portId", std::string{});
      const auto role = data.at("role").get<std::string>();
      ensure(!id.empty() && ((port.empty() && role == "body") || (!port.empty() && role == "port")), id,
             "A body has no port ID; a port component must declare one.");
      if (!port.empty())
        ports.insert(port);
      if (data.contains("pieceIds")) {
        const auto &filter = data.at("pieceIds");
        ensure(filter.is_array() && !filter.empty() && filter.size() <= definition_->pieces.size(), id,
               "pieceIds must be a nonempty list of pieces in this domain.");
        std::set<std::string> selected;
        for (const auto &entry : filter) {
          ensure(entry.is_string(), id, "pieceIds entries must be strings.");
          const auto piece_id = entry.get<std::string>();
          ensure(
              selected.insert(piece_id).second &&
                  std::any_of(definition_->pieces.begin(), definition_->pieces.end(),
                              [&](const auto &piece) { return piece.id == piece_id && piece.domain == d; }),
              id, "pieceIds must name distinct existing pieces in this domain.");
        }
        ensure(port.empty(), id, "Port bindings must cover every piece in their domain.");
      }
      const auto &track_binding = data.at("operationTracks");
      ensure(track_binding.is_object() || (shared_tracks && track_binding.is_string()), id,
             "Tracks require an explicit table or a declared shared track capability.");
      for (const auto &q : domain.placements)
        ensure(port.empty() || q.ports.contains(port), id, "Component refers to an undeclared local port.");
      // Meshes and materials are independent of a rigid pose table. Components
      // in one domain may share the same named tracks and identical frames;
      // validate and store that pose table once, including all endpoint checks.
      if (track_binding.is_string() && data.value("symmetries", Json::array()).empty()) {
        const auto previous = std::find_if(bindings[d].begin(), bindings[d].end(), [&](const auto &entry) {
          return entry.second.at("operationTracks") == track_binding &&
                 entry.second.at("placementFrames") == data.at("placementFrames") &&
                 entry.second.value("symmetries", Json::array()).empty();
        });
        if (previous != bindings[d].end()) {
          bindings[d].push_back({previous->first, data});
          continue;
        }
      }
      Component component;
      component.domain = d;
      const auto &frames = data.at("placementFrames");
      total_frames += frames.size();
      ensure(total_frames <= 262144, id, "Combined component frames exceed the realization limit.");
      ensure(frames.size() == domain.placements.size(), id,
             "Resting frames must cover exactly every placement.");
      for (const auto &q : domain.placements) {
        ensure(port.empty() || q.ports.contains(port), id, "Component refers to an undeclared local port.");
        component.frames.push_back(frame(frames.at(q.key)));
        const auto &m = component.frames.back();
        ensure(
            !flat_surface_ ||
                (std::abs(m[2]) + std::abs(m[6]) + std::abs(m[8]) + std::abs(m[9]) + std::abs(m[14]) < 1e-9 &&
                 flat_chart_contains(m[12], m[13])),
            id, "Flat-surface frames must be planar and anchored in the fundamental chart.");
        ensure(!spherical || (m[12] == 0 && m[13] == 0 && m[14] == 0), id,
               "Spherical component frames must rotate about the sphere origin.");
      }
      std::vector<Frame> symmetries{identity()};
      const auto declared = data.value("symmetries", Json::array());
      ensure(declared.is_array() && declared.size() <= 32, id,
             "At most 32 component symmetries are supported.");
      const auto &mesh = assets.at(asset_id);
      ensure(declared.empty() || (mesh.vertices.size() <= 4096 && mesh.boundary.size() <= 16384), id,
             "Symmetry verification requires a bounded component mesh.");
      for (const auto &j : declared) {
        const auto s = frame(j);
        for (const auto &v : assets.at(asset_id).vertices)
          ensure(std::any_of(assets.at(asset_id).vertices.begin(), assets.at(asset_id).vertices.end(),
                             [&](Point w) {
                               const auto delta = add(transform(s, v), scale(w, -1));
                               return dot(delta, delta) < 1e-12;
                             }),
                 id, "A symmetry must preserve this component's labeled mesh.");
        for (const auto &edge : mesh.boundary) {
          const auto a = transform(s, edge.a), b = transform(s, edge.b);
          const auto n = add(transform(s, edge.normal), scale(column(s, 3), -1));
          ensure(std::any_of(mesh.boundary.begin(), mesh.boundary.end(),
                             [&](const Asset::Edge &other) {
                               const auto da = add(a, scale(other.a, -1)), db = add(b, scale(other.b, -1));
                               return edge.radial == other.radial && dot(da, da) < 1e-12 &&
                                      dot(db, db) < 1e-12 && dot(n, other.normal) > 1 - 1e-10;
                             }),
                 id, "A symmetry must preserve oriented mesh boundaries and normals.");
        }
        symmetries.push_back(s);
      }
      ensure(!track_binding.is_string() || track_tables.contains(track_binding.get<std::string>()), id,
             "Unknown shared operation track table.");
      const auto &tracks =
          track_binding.is_string() ? track_tables.at(track_binding.get<std::string>()) : track_binding;
      ensure(tracks.is_object(), id, "A shared operation track table must be an object.");
      ensure(tracks.size() == definition_->operations.size(), id, "Tracks must cover every operation.");
      for (const auto &op : definition_->operations) {
        const auto &entries = tracks.at(op.id);
        GeometryTrack track;
        track.path_library = path_library;
        const bool selected=entries.is_object() && entries.value("kind",Json{})=="transport-regions";
        const auto named_path=[&](const std::string &name) -> const PreparedPath * {
          ensure(shared_tracks && rotation_tracks.contains(name),id,"Unknown shared rotation path.");
          auto found=named_paths.find(name);
          if(found==named_paths.end()) found=named_paths.emplace(name,prepare_path(rotation_tracks.at(name),id)).first;
          return &found->second;
        };
        std::vector<std::pair<const Operation *,const PreparedPath *>> selectors;
        if(selected) {
          ensure(std::find(capabilities.begin(),capabilities.end(),"transport-path-selectors")!=capabilities.end(),id,
            "Transport regions require the transport-path-selectors capability.");
          ensure(entries.size()==2 && entries.at("regions").is_array() && entries.at("regions").size()<=8,id,"Supply at most eight transport regions.");
          for(const auto &region:entries.at("regions")) {
            const auto request=region.at("request").get<std::string>();
            ensure(definition_->operation_ids.contains(request),id,"Unknown region transport request.");
            const auto *support=&definition_->operations[definition_->operation_ids.at(request)];
            const auto *path=named_path(region.at("path").get<std::string>());
            selectors.emplace_back(support,path);
            track.regions.push_back({support->roles[d],path->index});
          }
        } else track.placement_paths.resize(domain.placements.size());
        std::size_t count = 0;
        for (Index q = 0; q < domain.placements.size(); ++q) {
          const PreparedPath *prepared=nullptr;
          if(selected) {
            for(const auto &[support,path]:selectors) if(support->roles[d][q]==1) {
              ensure(op.roles[d][q]==1 && op.maps[d][q]==support->maps[d][q],id,"A region disagrees with its operation's transport.");
              ensure(!prepared || path_library->at(prepared->index)==path_library->at(path->index),id,"Overlapping regions disagree on the signed path.");
              prepared=path;
            }
          }
          if (op.roles[d][q] != 1)
            continue;
          ++count;
          PreparedPath inline_path{};
          if(selected) ensure(prepared,id,"Transport regions omit a participating placement.");
          else {
            const auto &path=entries.at(domain.placements[q].key);
            ensure(path.is_array() || (path.is_object() && path.contains("family")) || (shared_tracks && path.is_string()),id,
              "A rotation path requires steps or a declared shared track reference.");
            if(path.is_string()) prepared=named_path(path.get<std::string>());
            else { inline_path=prepare_path(path,id);prepared=&inline_path; }
            track.placement_paths[q]=prepared->index;
          }
          const auto &steps = path_library->at(prepared->index);
          total_steps += steps.size();
          ensure(total_steps <= 524288, id, "Combined rotation tracks exceed the realization limit.");
          auto endpoint = component.frames[q];
          for (Index s = 0; s < steps.size(); ++s)
            endpoint = steps[s].local ? multiply(endpoint, prepared->matrices[s])
                                      : multiply(prepared->matrices[s], endpoint);
          const auto &target = component.frames[op.maps[d][q]];
          if (flat_surface_)
            endpoint = canonical_flat_frame(endpoint);
          if (diagram_)
            for (int i = 12; i < 15; ++i)
              endpoint[i] = target[i];
          ensure(std::any_of(symmetries.begin(), symmetries.end(),
                             [&](const Frame &s) { return close(endpoint, multiply(target, s)); }),
                 op.id + "/" + id + "/" + domain.placements[q].key,
                 "Track endpoint disagrees with the exact transport.");
        }
        ensure(selected || entries.size() == count, id, "Tracks must name exactly the participating source placements for " + op.id +
          " (expected " + std::to_string(count) + ", got " + std::to_string(entries.size()) + ").");
        component.tracks.push_back(std::move(track));
      }
      const auto index = static_cast<Index>(components_.size());
      components_.push_back(std::move(component));
      bindings[d].push_back({index, data});
    }
    std::set<std::string> expected;
    for (const auto &[port, _] : domain.placements[0].ports)
      expected.insert(port);
    ensure(ports == expected, domain.id, "Components must bind every labeled port at least once.");
  }
  for (Index p = 0; p < definition_->pieces.size(); ++p) {
    const auto &piece = definition_->pieces[p];
    for (const auto &[index, data] : bindings[piece.domain]) {
      if (data.contains("pieceIds") &&
          std::none_of(data.at("pieceIds").begin(), data.at("pieceIds").end(),
                       [&](const Json &entry) { return entry.get<std::string>() == piece.id; }))
        continue;
      const auto port = data.value("portId", std::string{}), role = data.at("role").get<std::string>();
      const auto id = piece.id + "/" + data.at("id").get<std::string>();
      parts_.push_back({p, port, role, "", id, index});
      Json part{{"visualPartId", id},
                {"pieceId", piece.id},
                {"meshAssetId", data.at("meshAssetId")},
                {"materialBindingId",
                 port.empty() ? data.value("materialBindingId", std::string{"body"}) : piece.labels.at(port)},
                {"role", role}};
      if (!port.empty())
        part["portId"] = port;
      if (diagram_) {
        part["label"] = piece.id + (port.empty() ? "" : " · " + port);
        part["labelScale"] = 0.3;
      }
      scene_["visualParts"].push_back(part);
    }
  }
  scene_["ambientSpace"] = spherical ? "S2" : "R3";
  if (spherical)
    scene_["sphereRadius"] = radius;
  scene_["fidelity"] = realization.value("fidelity", std::string{"idealized-articulated"});
  scene_["catalogTransportVerified"] = true;
}
} // namespace twisty
