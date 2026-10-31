#include "geometry_compiler.hpp"
#include "backend_registry.hpp"
#include "../artifact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace twisty {
namespace {
GeometryMatrix multiply(const GeometryMatrix &a, const GeometryMatrix &b) {
  GeometryMatrix out{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int k = 0; k < 4; ++k)
        out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
  return out;
}
} // namespace
GeometryMatrix geometry_rotation(const GeometryRotation &step, double progress) {
  if (step.plane) {
    const auto &u = step.plane->at(0), &v = step.plane->at(1);
    const double c = std::cos(step.angle * progress) - 1, s = std::sin(step.angle * progress);
    GeometryMatrix out{};
    for (int j = 0; j < 4; ++j)
      for (int i = 0; i < 4; ++i)
        out[j * 4 + i] =
            (i == j ? 1. : 0.) + c * (u[i] * u[j] + v[i] * v[j]) + s * (v[i] * u[j] - u[i] * v[j]);
    return out;
  }
  const auto &n = step.axis;
  const double angle = step.angle * progress, c = std::cos(angle), s = std::sin(angle);
  GeometryMatrix out{};
  for (int col = 0; col < 3; ++col) {
    GeometryPoint v{};
    v[col] = 1;
    const GeometryPoint cross{n[1] * v[2] - n[2] * v[1], n[2] * v[0] - n[0] * v[2],
                              n[0] * v[1] - n[1] * v[0]};
    for (int row = 0; row < 3; ++row)
      out[col * 4 + row] = v[row] * c + cross[row] * s + n[row] * n[col] * (1 - c);
  }
  for (int row = 0; row < 3; ++row) {
    out[12 + row] = step.pivot[row];
    for (int col = 0; col < 3; ++col)
      out[12 + row] -= out[col * 4 + row] * step.pivot[col];
  }
  out[15] = 1;
  return out;
}
namespace {
double progress_value(double progress) {
  if (!std::isfinite(progress))
    throw DiagnosticError("animation.progress", "/progress", "Animation progress must be finite.");
  return std::clamp(progress, 0.0, 1.0);
}
std::vector<double> state_poses(const GeometryProgram &program, const State &state) {
  std::vector<double> poses;
  poses.reserve(program.parts.size() * 16);
  for (const auto &part : program.parts) {
    const auto &pose = program.tables[part.table].frames[state.placement_of[part.piece]];
    poses.insert(poses.end(), pose.begin(), pose.end());
  }
  return poses;
}
} // namespace

GeometryDefinition::GeometryDefinition(std::shared_ptr<const Execution> execution,
                                       const std::string &realization) {
  if (!execution)
    throw DiagnosticError("realization.definition", "/definition", "Expected an execution definition.");
  const auto document = execution->source_free() ? artifact::detail::parse_geometry_document(realization) : Json::parse(realization);
  if (execution->source_free()) {
    artifact::detail::fields(document, {"schemaVersion", "id", "kind", "compatibleDefinitionDigest", "compatibleInterpretationDigest", "requiredCapabilities", "explicitCubeTracks"});
    artifact::detail::id(document.at("id"));
    if (execution->interpretation_digest().empty() || document.at("kind") != "artifact-cube-euclidean" ||
        document.at("compatibleInterpretationDigest") != execution->interpretation_digest())
      throw DiagnosticError("realization.digest_mismatch", "/compatibleInterpretationDigest", "This artifact realization requires its exact interpretation manifest.");
    const auto caps = document.at("requiredCapabilities").get<std::vector<std::string>>();
    if (caps.size() != 3 || std::set<std::string>(caps.begin(), caps.end()) !=
        std::set<std::string>{"triangle-meshes", "rigid-transforms", "explicit-cube-tracks"})
      throw DiagnosticError("realization.capability", "/requiredCapabilities", "Artifact cube realizations require the declared three capabilities.");
  }
  else if (document.value("kind", "") == "artifact-cube-euclidean")
    throw DiagnosticError("realization.definition", "/kind", "Artifact geometry requires an artifact execution context.");
  const auto digest = execution->source_free() ? sha256(realization) : std::string{};
  program_ = detail::compile_geometry_backend(std::move(execution), document, digest);
}
GeometryDefinition::GeometryDefinition(std::shared_ptr<const Execution> execution,
                                       const std::string &interpretation, const std::string &realization)
    : GeometryDefinition(execution ? execution->with_interpretation(interpretation) : nullptr, realization) {}
std::string GeometryDefinition::scene_json() const {
  return program_->scene.dump();
}
std::string GeometryDefinition::space_json() const {
  return program_->space.dump();
}
const std::vector<float> &GeometryDefinition::positions() const {
  return program_->positions;
}
const std::vector<float> &GeometryDefinition::normals() const {
  return program_->normals;
}
const std::vector<std::uint32_t> &GeometryDefinition::indices() const {
  return program_->indices;
}
GeometryIntrinsicMesh GeometryDefinition::intrinsic_mesh() const {
  if (!program_->intrinsic)
    throw DiagnosticError("geometry.intrinsic_mesh", "/definition",
                          "This realization does not expose intrinsic meshes.");
  return GeometryIntrinsicMesh(program_->intrinsic);
}
std::string GeometryIntrinsicMesh::descriptor_json() const { return data_->descriptor.dump(); }
const std::vector<float> &GeometryIntrinsicMesh::positions() const { return data_->positions; }
const std::vector<float> &GeometryIntrinsicMesh::normals() const { return data_->normals; }
const std::vector<std::uint32_t> &GeometryIntrinsicMesh::indices() const { return data_->indices; }
GeometryFrame GeometryDefinition::frame(const std::string &state_json) const {
  const auto state = program_->execution->decode(Json::parse(state_json));
  GeometryFrame result;
  result.program_ = program_;
  result.isometries_ = state_poses(*program_, state);
  return result;
}
GeometryAnimation GeometryDefinition::animate(const std::string &input) const {
  const auto transition = Json::parse(input);
  const auto execution = program_->execution;
  const auto definition = execution->definition();
  if (transition.at(execution->source_free() ? "executionDigest" : "definitionDigest") != definition->digest)
    throw DiagnosticError("realization.digest_mismatch", "/transition",
                          "Transition uses a different definition.");
  auto before = execution->decode(transition.at("beforeState"));
  auto after = execution->decode(transition.at("afterState"));
  const auto operation = transition.at("request").at("operation").get<std::string>();
  if (!transition.at("request").contains("choices"))
    throw DiagnosticError("realization.witness", "/request/choices",
                          "Witnesses must record the completed tuple.");
  const auto planned = execution->plan(before, operation, transition.at("request").at("choices"));
  if (!planned.transition || planned.transition->after != after ||
      execution->encode(*planned.transition) != transition)
    throw DiagnosticError("realization.witness", "/transition",
                          "Transition is not the recorded abstract witness.");
  auto motion = std::make_shared<GeometryMotion>();
  motion->program = program_;
  motion->before = std::move(before);
  motion->after = std::move(after);
  motion->piece_operations.resize(definition->pieces.size());
  motion->moving.resize(definition->pieces.size(), false);
  for (Index i = 0; i < planned.transition->actions.size(); ++i) {
    const auto &action = planned.transition->actions[i];
    const auto track = execution->event_operation(*planned.transition, i);
    const auto domain = definition->pieces[action.piece].domain;
    const auto &certified = definition->operations[track];
    if (certified.roles[domain][action.from] != 1 || certified.maps[domain][action.from] != action.to)
      throw DiagnosticError("realization.witness", "/pieceActions/event",
                            "Event track does not certify this exact participation row.");
    motion->piece_operations[action.piece] = track;
    motion->moving[action.piece] = true; // Includes self-loop participants.
  }
  double extent = program_->minimum_motion_extent;
  for (const auto &part : program_->parts)
    if (motion->moving[part.piece]) {
      const auto &track = program_->tables[part.table].tracks[motion->piece_operations[part.piece]];
      double travel = 0;
      for (const auto &step : track.at(motion->before.placement_of[part.piece]))
        travel += std::abs(step.angle);
      extent = std::max(extent, std::max(travel, track.extent_hint));
    }
  motion->duration_hint = 220 * extent / (std::numbers::pi / 2);
  GeometryAnimation result;
  result.motion_ = std::move(motion);
  return result;
}
std::string GeometryDefinition::resolve_part_json(const std::string &id) const {
  const auto found = program_->part_ids.find(id);
  if (found == program_->part_ids.end())
    return Json{{"status", "NoTarget"}}.dump();
  const auto &part = program_->scene.at("visualParts").at(found->second);
  Json result{{"status", "Target"}, {"pieceId", part.at("pieceId")}};
  if (part.contains("portId"))
    result["portId"] = part.at("portId");
  return result.dump();
}
GeometryDrawing GeometryDefinition::project(const GeometryFrame &frame, const std::string &projection) const {
  return project_with_options(frame, projection, "{}");
}
GeometryDrawing GeometryDefinition::project_with_options(const GeometryFrame &frame,
                                                         const std::string &projection,
                                                         const std::string &options_json) const {
  if (frame.program_ != program_)
    throw DiagnosticError("geometry.frame_mismatch", "/frame",
                          "Frame belongs to a different geometry definition.");
  GeometryDrawing result;
  result.program_ = program_;
  const auto options = Json::parse(options_json);
  if (!options.is_object())
    throw DiagnosticError("geometry.projection", "/options", "Projection options must be an object.");
  if (program_->s3) {
    if (projection != "stereographic")
      throw DiagnosticError("geometry.projection", "/projection", "S3 requires stereographic projection.");
    result.mesh_ = project_s3(*program_, frame.isometries_, options);
    result.transforms_.resize(frame.isometries_.size(), 0);
    for (std::size_t i = 0; i < result.transforms_.size(); i += 16)
      for (int j = 0; j < 4; ++j)
        result.transforms_[i + j * 5] = 1;
    return result;
  }
  if (projection != "embedding" || !options.empty())
    throw DiagnosticError("geometry.projection", "/projection",
                          "This definition supports only the embedding projection without options.");
  result.transforms_.resize(frame.isometries_.size());
  for (std::size_t part = 0; part < program_->parts.size(); ++part)
    for (int i = 0; i < 16; ++i)
      result.transforms_[part * 16 + i] = static_cast<float>(
          frame.isometries_[part * 16 + i] * (i < 12 && i % 4 < 3 ? program_->parts[part].mesh_scale : 1));
  return result;
}
GeometryFrame GeometryAnimation::evaluate(double progress) const {
  const double t = progress_value(progress);
  GeometryFrame result;
  result.program_ = motion_->program;
  result.isometries_.reserve(result.program_->parts.size() * 16);
  for (const auto &part : result.program_->parts) {
    const auto &table = result.program_->tables[part.table];
    const auto from = motion_->before.placement_of[part.piece], to = motion_->after.placement_of[part.piece];
    auto pose = table.frames[t == 1 ? to : from];
    if (t > 0 && t < 1 && motion_->moving[part.piece]) {
      const auto &path = table.tracks[motion_->piece_operations[part.piece]].at(from);
      for (const auto &step : path) {
        const auto r = geometry_rotation(step, t);
        pose = step.local ? multiply(pose, r) : multiply(r, pose);
      }
      if (table.linear_translation)
        for (int i = 12; i < 15; ++i)
          pose[i] = table.frames[from][i] + (table.frames[to][i] - table.frames[from][i]) * t;
    }
    result.isometries_.insert(result.isometries_.end(), pose.begin(), pose.end());
  }
  return result;
}
double GeometryAnimation::duration_hint() const {
  return motion_->duration_hint;
}
std::string GeometryFrame::space_json() const {
  return program_->space.dump();
}
std::string GeometryDrawing::scene_json() const {
  return (mesh_ ? mesh_->scene : program_->scene).dump();
}
const std::vector<float> &GeometryDrawing::positions() const {
  return mesh_ ? mesh_->positions : program_->positions;
}
const std::vector<float> &GeometryDrawing::normals() const {
  return mesh_ ? mesh_->normals : program_->normals;
}
const std::vector<std::uint32_t> &GeometryDrawing::indices() const {
  return mesh_ ? mesh_->indices : program_->indices;
}

GeometryInstance::GeometryInstance(std::shared_ptr<const Execution> execution, const std::string &realization)
    : GeometryInstance(std::make_shared<GeometryDefinition>(std::move(execution), realization)) {}
GeometryInstance::GeometryInstance(std::shared_ptr<const GeometryDefinition> definition)
    : definition_(std::move(definition)) {
  if (!definition_)
    throw DiagnosticError("realization.definition", "/definition", "Expected a geometry definition.");
  const auto abstract = definition_->program_->execution->definition();
  set_state_json(encode_state(*abstract, abstract->initial).dump());
}
void GeometryInstance::set_state_json(const std::string &state) {
  auto frame = definition_->frame(state);
  auto drawing = definition_->project(frame, definition_->program_->s3 ? "stereographic" : "embedding");
  auto decoded = definition_->program_->execution->decode(Json::parse(state));
  displayed_ = before_ = after_ = std::move(decoded);
  animation_.reset();
  drawing_ = std::move(drawing);
}
std::string GeometryInstance::prepare_animation_json(const std::string &input) {
  try {
    auto animation = definition_->animate(input);
    auto drawing = definition_->project(animation.evaluate(0),
                                        definition_->program_->s3 ? "stereographic" : "embedding");
    const auto &motion = *animation.motion_;
    before_ = motion.before;
    after_ = motion.after;
    displayed_ = before_;
    const auto duration = animation.duration_hint();
    animation_ = std::move(animation);
    drawing_ = std::move(drawing);
    return Json{{"status", "Prepared"}, {"durationHint", duration}}.dump();
  } catch (const std::exception &error) {
    auto result = diagnostic_result(error);
    result["status"] = "UnsupportedTransition";
    return result.dump();
  }
}
void GeometryInstance::sample(double progress) {
  const double t = progress_value(progress);
  if (animation_)
    drawing_ = definition_->project(animation_->evaluate(t * t * (3 - 2 * t)),
                                    definition_->program_->s3 ? "stereographic" : "embedding");
  displayed_ = animation_ && t == 1 ? after_ : before_;
}
std::string GeometryInstance::bind_hit_json(const std::string &id) const {
  auto target = Json::parse(definition_->resolve_part_json(id));
  if (target.at("status") != "Target")
    return target.dump();
  const auto &program = *definition_->program_;
  const auto &definition = *program.execution->definition();
  const auto p = program.parts[program.part_ids.at(id)].piece;
  const auto &piece = definition.pieces[p];
  const auto q = displayed_.placement_of[p];
  const auto &placement = definition.domains[piece.domain].placements[q];
  target["operationCandidates"] = Json::array();
  for (const auto &op : definition.operations) {
    const bool candidate =
        definition.port_catalog
            ? op.roles[piece.domain][q] == 1
            : std::any_of(placement.footprint.begin(), placement.footprint.end(), [&](const auto &cell) {
                return std::find(op.cells.begin(), op.cells.end(), cell) != op.cells.end();
              });
    if (candidate)
      target["operationCandidates"].push_back({{"operation", op.id}, {"parameters", Json::object()}});
  }
  return target.dump();
}
} // namespace twisty
