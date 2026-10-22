#pragma once
#include "twisty/execution.hpp"

namespace twisty {
struct GeometryProgram;
struct GeometryMotion;
class GeometryDefinition;

// An owned, space-qualified pose snapshot. Matrices are intrinsic isometries;
// they are not renderer model matrices. Inspect space_json() before using them.
class GeometryFrame {
public:
  std::string space_json() const;
  const std::vector<double> &isometries() const {
    return isometries_;
  }

private:
  GeometryFrame() = default;
  friend class GeometryDefinition;
  friend class GeometryAnimation;
  std::shared_ptr<const GeometryProgram> program_;
  std::vector<double> isometries_;
};

// Renderer-ready output of a named projection. Owns its matrices and shares
// immutable assets. A future projection may supply different mesh buffers.
class GeometryDrawing {
public:
  std::string scene_json() const;
  const std::vector<float> &positions() const;
  const std::vector<float> &normals() const;
  const std::vector<std::uint32_t> &indices() const;
  const std::vector<float> &transforms() const {
    return transforms_;
  }

private:
  GeometryDrawing() = default;
  friend class GeometryDefinition;
  friend class GeometryInstance;
  std::shared_ptr<const GeometryProgram> program_;
  std::vector<float> transforms_;
};

// Prepared exact witness; evaluate is pure, linear in progress, and seekable.
// The caller owns timing and easing. Progress is clamped to [0,1].
class GeometryAnimation {
public:
  GeometryFrame evaluate(double progress) const;
  double duration_hint() const;

private:
  GeometryAnimation() = default;
  friend class GeometryDefinition;
  friend class GeometryInstance;
  std::shared_ptr<const GeometryMotion> motion_;
};

// Immutable compiled geometric data. Copies and animations share its storage.
class GeometryDefinition {
public:
  GeometryDefinition(const std::string &definition_json, const std::string &realization_json);
  GeometryDefinition(std::shared_ptr<const Execution>, const std::string &realization_json);
  std::string scene_json() const;
  std::string space_json() const;
  const std::vector<float> &positions() const;
  const std::vector<float> &normals() const;
  const std::vector<std::uint32_t> &indices() const;
  GeometryFrame frame(const std::string &state_json) const;
  GeometryAnimation animate(const std::string &transition_record) const;
  GeometryDrawing project(const GeometryFrame &, const std::string &projection = "embedding") const;
  std::string resolve_part_json(const std::string &visual_part_id) const;

private:
  friend class GeometryInstance;
  std::shared_ptr<const GeometryProgram> program_;
};

// Stateful compatibility facade. New consumers can use definition.frame() and
// animation.evaluate() directly. sample() retains the original smoothstep easing.
class GeometryInstance {
public:
  GeometryInstance(const std::string &definition_json, const std::string &realization_json);
  GeometryInstance(std::shared_ptr<const Execution>, const std::string &realization_json);
  explicit GeometryInstance(std::shared_ptr<const GeometryDefinition>);
  std::shared_ptr<const GeometryDefinition> definition() const {
    return definition_;
  }
  std::string scene_json() const {
    return definition_->scene_json();
  }
  void set_state_json(const std::string &);
  std::string prepare_animation_json(const std::string &);
  void sample(double progress);
  std::string bind_hit_json(const std::string &visual_part_id) const;
  const std::vector<float> &positions() const {
    return definition_->positions();
  }
  const std::vector<float> &normals() const {
    return definition_->normals();
  }
  const std::vector<std::uint32_t> &indices() const {
    return definition_->indices();
  }
  const std::vector<float> &transforms() const {
    return drawing_.transforms();
  }

private:
  std::shared_ptr<const GeometryDefinition> definition_;
  std::optional<GeometryAnimation> animation_;
  GeometryDrawing drawing_;
  State displayed_, before_, after_;
};
using Geometry = GeometryInstance;
} // namespace twisty
