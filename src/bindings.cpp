#include "twisty/kernel.hpp"
#include <emscripten/bind.h>

using namespace emscripten;
using namespace twisty;
namespace {
val positions(const Geometry &geometry) {
  return val(typed_memory_view(geometry.positions().size(), geometry.positions().data()));
}
val normals(const Geometry &geometry) {
  return val(typed_memory_view(geometry.normals().size(), geometry.normals().data()));
}
val indices(const Geometry &geometry) {
  return val(typed_memory_view(geometry.indices().size(), geometry.indices().data()));
}
val transforms(const Geometry &geometry) {
  return val(typed_memory_view(geometry.transforms().size(), geometry.transforms().data()));
}
std::string compile(const std::string &source) {
  return compile_json(source).dump();
}
std::string kernel_info_json() {
  return kernel_info().dump();
}
Geometry *create_geometry(const Session &session, const std::string &realization) {
  return new Geometry(session.definition(), realization);
}
} // namespace
EMSCRIPTEN_BINDINGS(twisty) {
  function("compileJSON", &compile);
  function("kernelInfoJSON", &kernel_info_json);
  function("createGeometry", &create_geometry, return_value_policy::take_ownership());
  class_<Session>("Session")
      .constructor<std::string>()
      .function("snapshotJSON", &Session::snapshot_json)
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
      .constructor<std::string, std::string>()
      .function("sceneJSON", &Geometry::scene_json)
      .function("setStateJSON", &Geometry::set_state_json)
      .function("prepareAnimationJSON", &Geometry::prepare_animation_json)
      .function("sample", &Geometry::sample)
      .function("bindHitJSON", &Geometry::bind_hit_json)
      .function("positions", &positions)
      .function("normals", &normals)
      .function("indices", &indices)
      .function("transforms", &transforms);
}
