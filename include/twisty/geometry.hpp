#pragma once
#include "twisty/core.hpp"
#include <array>

namespace twisty {
class Geometry {
public:
  Geometry(const std::string &definition_json, const std::string &realization_json);
  std::string scene_json() const {
    return scene_.dump();
  }
  void set_state_json(const std::string &);
  std::string prepare_animation_json(const std::string &);
  void sample(double progress);
  std::string bind_hit_json(const std::string &visual_part_id) const;
  const std::vector<float> &positions() const {
    return positions_;
  }
  const std::vector<float> &normals() const {
    return normals_;
  }
  const std::vector<std::uint32_t> &indices() const {
    return indices_;
  }
  const std::vector<float> &transforms() const {
    return transforms_;
  }

private:
  struct Part {
    Index piece;
    std::string port;
    std::string kind;
    std::string component;
    std::string id;
  };
  std::shared_ptr<const Definition> definition_;
  Json scene_;
  bool diagram_ = false;
  State displayed_;
  State before_;
  State after_;
  bool animated_ = false;
  std::string family_;
  bool inverse_ = false;
  std::vector<bool> moving_;
  std::vector<Part> parts_;
  std::vector<float> positions_;
  std::vector<float> normals_;
  std::vector<std::uint32_t> indices_;
  std::vector<float> transforms_;
  std::array<double, 16> resting(const Part &, const State &) const;
};
} // namespace twisty
