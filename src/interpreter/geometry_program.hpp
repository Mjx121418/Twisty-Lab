#pragma once
#include "twisty/geometry.hpp"
#include <array>

namespace twisty {
using GeometryMatrix = std::array<double, 16>;
using GeometryPoint = std::array<double, 3>;
struct GeometryRotation {
  GeometryPoint axis, pivot;
  double angle;
  bool local;
  bool operator==(const GeometryRotation &) const = default;
};
void validate_motion_families(const Json &realization);
GeometryRotation family_rotation(const Json &realization, const Json &binding);
struct GeometryTrack {
  // A uniform path or placement-indexed shared paths, never both.
  double extent_hint = 0;
  std::vector<GeometryRotation> uniform;
  std::vector<Index> placement_paths;
  std::shared_ptr<const std::vector<std::vector<GeometryRotation>>> path_library;
  struct Region { TransportRoles support; Index path; };
  std::vector<Region> regions;
  const std::vector<GeometryRotation> &at(Index q) const {
    if (!regions.empty()) {
      for (const auto &region : regions)
        if (region.support[q] == 1) return path_library->at(region.path);
      return path_library->at(0);
    }
    return placement_paths.empty() ? uniform : path_library->at(placement_paths.at(q));
  }
};
struct GeometryPoseTable {
  std::vector<GeometryMatrix> frames;
  std::vector<GeometryTrack> tracks;
  bool linear_translation = false;
};
struct GeometryPart {
  Index piece, table;
  // Local mesh scale/decoration is separate from the intrinsic isometry.
  double mesh_scale = 1;
};
struct GeometryProgram {
  std::shared_ptr<const Execution> execution;
  Json scene, space;
  std::vector<float> positions, normals;
  std::vector<std::uint32_t> indices;
  std::vector<GeometryPart> parts;
  std::vector<GeometryPoseTable> tables;
  std::map<std::string, Index> part_ids;
  double minimum_motion_extent = 0;
};
struct GeometryMotion {
  std::shared_ptr<const GeometryProgram> program;
  State before, after;
  std::vector<Index> piece_operations;
  std::vector<bool> moving;
  double duration_hint = 0;
};
} // namespace twisty
