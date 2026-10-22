#pragma once
// Realization-specific authoring and validation. No presentation state lives here.
#include "geometry_program.hpp"

namespace twisty {
class GeometryCompiler {
public:
  GeometryCompiler(std::shared_ptr<const Execution>, const std::string &realization_json);
  std::shared_ptr<const GeometryProgram> compile();

private:
  std::shared_ptr<const Execution> execution_;
  struct Part {
    Index piece;
    std::string port;
    std::string kind;
    std::string component;
    std::string id;
    Index frame_index = 0;
  };
  std::shared_ptr<const Definition> definition_;
  Json scene_;
  bool diagram_ = false;
  bool catalog_ = false;
  bool spherical_ = false;
  bool catalog_spherical_ = false;
  bool articulated_ = false;
  bool flat_surface_ = false;
  bool flat_twisted_ = false;
  bool flat_rhombus_ = false;
  bool flat_pillowcase_ = false;
  double flat_side_ = 2;
  using Frame = std::array<double, 16>;
  using Point = std::array<double, 3>;
  struct Model {
    std::string id;
    std::vector<Point> vertices;
    std::vector<Point> surface_vertices;
    std::map<std::string, Point> port_centers;
    std::map<std::string, Point> port_normals;
    std::vector<Frame> symmetries;
  };
  struct Track {
    Point axis;
    double angle;
  };
  using Rotation = GeometryRotation;
  struct Component {
    Index domain;
    std::vector<Frame> frames;
    std::vector<std::vector<std::vector<Rotation>>> paths;
  };
  std::vector<Component> components_;
  std::vector<Model> models_;
  std::vector<Index> model_of_domain_;
  std::vector<std::vector<Frame>> placement_frames_;
  std::vector<Track> tracks_;
  std::vector<std::vector<Frame>> spherical_frames_;
  double scale_ = 1;
  std::vector<Part> parts_;
  std::vector<float> positions_;
  std::vector<float> normals_;
  std::vector<std::uint32_t> indices_;
  std::array<double, 16> resting(const Part &, const State &) const;
  void build_catalog(const Json &);
  void build_spherical(const Json &);
  void build_catalog_spherical(const Json &);
  void build_articulated(const Json &);
  void build_flat_surface(const Json &);
  Frame canonical_flat_frame(Frame) const;
  bool flat_chart_contains(double x, double y) const;
  Frame catalog_resting(const Part &, const State &) const;
  void validate_catalog_transport(Index operation) const;
};
} // namespace twisty
