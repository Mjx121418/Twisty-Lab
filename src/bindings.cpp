#include "twisty/artifact.hpp"
#include "twisty/session.hpp"
#include "twisty/geometry.hpp"
#ifndef TWISTY_ARTIFACT_ONLY
#include "twisty/authoring/artifact.hpp"
#include "twisty/kernel.hpp"
#endif
#include <emscripten/bind.h>

using namespace emscripten;
using namespace twisty;
namespace {
template <class T> val positions(const T &geometry) {
  return val(typed_memory_view(geometry.positions().size(), geometry.positions().data()));
}
template <class T> val normals(const T &geometry) {
  return val(typed_memory_view(geometry.normals().size(), geometry.normals().data()));
}
template <class T> val indices(const T &geometry) {
  return val(typed_memory_view(geometry.indices().size(), geometry.indices().data()));
}
template <class T> val transforms(const T &geometry) {
  return val(typed_memory_view(geometry.transforms().size(), geometry.transforms().data()));
}
val isometries(const GeometryFrame &frame) {
  return val(typed_memory_view(frame.isometries().size(), frame.isometries().data()));
}
#ifndef TWISTY_ARTIFACT_ONLY
std::string compile_execution(const std::string &source) {
  return compile_execution_json(source).dump();
}
std::string kernel_info_json() {
  return kernel_info().dump();
}
#endif
std::string artifact_info_json() { return artifact::info().dump(); }
Session *create_artifact_session(const std::string &text) { return new Session(Execution::from_artifact(text)); }
GeometryDefinition *create_artifact_geometry(const Session &session, const std::string &manifest, const std::string &realization) {
  return new GeometryDefinition(session.execution(), manifest, realization);
}
GeometryDefinition *create_artifact_geometry_with_payload(const Session &session, const std::string &manifest,
    const std::string &realization, const std::string &payload) {
  return new GeometryDefinition(session.execution(), manifest, realization, payload);
}
Geometry *create_geometry(const Session &session, const std::string &realization) {
  return new Geometry(session.execution(), realization);
}
GeometryDefinition *create_geometry_definition(const Session &session, const std::string &realization) {
  return new GeometryDefinition(session.execution(), realization);
}
Geometry *create_geometry_instance(const GeometryDefinition &definition) {
  return new Geometry(std::make_shared<GeometryDefinition>(definition));
}
Session *create_session_from_definition(const Session &session) {
  return new Session(session.execution());
}
#ifndef TWISTY_ARTIFACT_ONLY
Controller *create_controller(const Session &session) { return new Controller(session.execution()); }
#endif
} // namespace
EMSCRIPTEN_BINDINGS(twisty) {
  function("artifactInfoJSON", &artifact_info_json);
  function("validateArtifactJSON", &artifact::validate_json);
  function("createArtifactSession", &create_artifact_session, return_value_policy::take_ownership());
  function("createArtifactGeometryDefinition", &create_artifact_geometry, return_value_policy::take_ownership());
  function("createArtifactGeometryWithPayload", &create_artifact_geometry_with_payload, return_value_policy::take_ownership());
#ifndef TWISTY_ARTIFACT_ONLY
  function("compileExecutionJSON", &compile_execution);
  function("compilePuzzleJSON", &twisty::language::compile_json);
  function("cubeModuleJSON", &twisty::language::cube_module_json);
  function("gearModuleJSON", &twisty::language::gear_module_json);
  function("kernelInfoJSON", &kernel_info_json);
  function("cubeArtifactBundleJSON", &language::cube_artifacts_json);
#endif
  function("createGeometry", &create_geometry, return_value_policy::take_ownership());
  function("createGeometryDefinition", &create_geometry_definition, return_value_policy::take_ownership());
  function("createGeometryInstance", &create_geometry_instance, return_value_policy::take_ownership());
  function("createSessionFromDefinition", &create_session_from_definition,
           return_value_policy::take_ownership());
#ifndef TWISTY_ARTIFACT_ONLY
  function("createController", &create_controller, return_value_policy::take_ownership());
  class_<Controller>("Controller")
      .function("catalogJSON", &Controller::catalog_json)
      .function("familyStateJSON", &Controller::family_state_json)
      .function("familiesForPiecesJSON", &Controller::families_for_pieces_json)
      .function("destinationsJSON", &Controller::destinations_json)
      .function("planToJSON", &Controller::plan_to_json)
      .function("turnToJSON", &Controller::turn_to_json);
  class_<twisty::language::Puzzle>("Puzzle")
      .constructor<std::string>()
      .function("inspectJSON", &twisty::language::Puzzle::inspect_json)
      .function("queryJSON", &twisty::language::Puzzle::query_json);
#endif
  class_<Session>("Session")
#ifndef TWISTY_ARTIFACT_ONLY
      .constructor<std::string>()
#endif
      .function("snapshotJSON", &Session::snapshot_json)
      .function("stateSnapshotJSON", &Session::state_snapshot_json)
      .function("lightweightResults", &Session::lightweight_results)
      .function("revision", &Session::revision)
      .function("definitionJSON", &Session::definition_json)
      .function("stateJSON", &Session::state_json)
      .function("executeJSON", &Session::execute_json)
      .function("planJSON", &Session::plan_json)
      .function("validateStateJSON", &Session::validate_state_json)
      .function("runJSON", &Session::run_json)
      .function("undoJSON", &Session::undo_json)
      .function("redoJSON", &Session::redo_json)
      .function("scrambleJSON", &Session::scramble_json)
      .function("saveJSON", &Session::save_json)
      .function("saveWithPresentationJSON", &Session::save_with_presentation_json)
      .function("loadJSON", &Session::load_json);
  class_<Geometry>("Geometry")
#ifndef TWISTY_ARTIFACT_ONLY
      .constructor<std::string, std::string>()
#endif
      .function("sceneJSON", &Geometry::scene_json)
      .function("setStateJSON", &Geometry::set_state_json)
      .function("prepareAnimationJSON", &Geometry::prepare_animation_json)
      .function("sample", &Geometry::sample)
      .function("bindHitJSON", &Geometry::bind_hit_json)
      .function("positions", &positions<Geometry>)
      .function("normals", &normals<Geometry>)
      .function("indices", &indices<Geometry>)
      .function("transforms", &transforms<Geometry>);
  class_<GeometryDefinition>("GeometryDefinition")
#ifndef TWISTY_ARTIFACT_ONLY
      .constructor<std::string, std::string>()
#endif
      .function("sceneJSON", &GeometryDefinition::scene_json)
      .function("spaceJSON", &GeometryDefinition::space_json)
      .function("intrinsicMesh", &GeometryDefinition::intrinsic_mesh)
      .function("frame", &GeometryDefinition::frame)
      .function("animate", &GeometryDefinition::animate)
      .function("project", &GeometryDefinition::project)
      .function("projectWithOptions", &GeometryDefinition::project_with_options)
      .function("resolvePartJSON", &GeometryDefinition::resolve_part_json)
      .function("positions", &positions<GeometryDefinition>)
      .function("normals", &normals<GeometryDefinition>)
      .function("indices", &indices<GeometryDefinition>);
  class_<GeometryIntrinsicMesh>("GeometryIntrinsicMesh")
      .function("descriptorJSON", &GeometryIntrinsicMesh::descriptor_json)
      .function("positions", &positions<GeometryIntrinsicMesh>)
      .function("normals", &normals<GeometryIntrinsicMesh>)
      .function("indices", &indices<GeometryIntrinsicMesh>);
  class_<GeometryFrame>("GeometryFrame")
      .function("spaceJSON", &GeometryFrame::space_json)
      .function("isometries", &isometries);
  class_<GeometryAnimation>("GeometryAnimation")
      .function("evaluate", &GeometryAnimation::evaluate)
      .function("durationHint", &GeometryAnimation::duration_hint);
  class_<GeometryDrawing>("GeometryDrawing")
      .function("sceneJSON", &GeometryDrawing::scene_json)
      .function("positions", &positions<GeometryDrawing>)
      .function("normals", &normals<GeometryDrawing>)
      .function("indices", &indices<GeometryDrawing>)
      .function("transforms", &transforms<GeometryDrawing>);
}
