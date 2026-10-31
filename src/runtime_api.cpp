#include "twisty/contracts.hpp"
#include "twisty/runtime_api.hpp"
#include "twisty/controller.hpp"
#include "artifact_internal.hpp"

namespace twisty {
std::shared_ptr<const Execution> runtime_execution(const std::string &artifact,
                                                  const std::string &controls) {
  return Execution::from_artifact(artifact)->with_controls(controls);
}
std::shared_ptr<const Execution> Execution::with_controls(const std::string &text) const {
  using namespace artifact::detail;
  if (!source_free()) throw DiagnosticError("controls.artifact", "/", "Runtime controls require an artifact execution.");
  try {
    auto document = artifact::parse_document(text);
    fields(document, {"kind", "contractVersion", "executionDigest", "controlDigest", "catalog"});
    require(document.at("kind") == contracts::controls_kind && document.at("contractVersion") == contracts::controls_version,
      "/", "Unsupported controller contract.", "controls.unsupported");
    require(document.at("executionDigest") == definition_->digest, "/executionDigest",
      "Controller pins another executable.", "controls.digest_mismatch");
    pin(document, "controlDigest");
    auto catalog = document.at("catalog");
    fields(catalog, {"version", "drivers", "families"});
    array(catalog.at("drivers"), 128);
    for (auto &driver : catalog["drivers"]) {
      fields(driver, {"id", "piece", "stops", "reader"});
      id(driver.at("id")); const auto stops = ids(driver.at("stops"), 128);
      const auto piece = index(driver.at("piece"));
      require(piece < puzzle_->bindings.pieces.size(), "/catalog/drivers/piece", "Unknown phase-reader piece.", "controls.reference");
      Json placements = Json::object();
      for (const auto &entry : array(driver.at("reader"))) {
        fields(entry, {"placement", "stop"});
        const auto placement = index(entry.at("placement")), stop = index(entry.at("stop"));
        require(placement < puzzle_->bindings.placements.size() && stop < stops.size(), "/catalog/drivers/reader", "Unknown reader placement or stop.", "controls.reference");
        const auto &key = puzzle_->bindings.placements[placement];
        require(!placements.contains(key), "/catalog/drivers/reader", "Duplicate phase-reader placement.", "controls.reference");
        placements[key] = stops[stop];
      }
      driver["piece"] = puzzle_->bindings.pieces[piece]; driver.erase("reader"); driver["placements"] = std::move(placements);
    }
    for (const auto &family : array(catalog.at("families"), 256)) {
      fields(family, {"id", "drivers", "routes"}, {"label"}); id(family.at("id")); ids(family.at("drivers"), 2);
      if (family.contains("label")) id(family.at("label"));
      for (const auto &route : array(family.at("routes"), 131072)) {
        fields(route, {"from", "to", "request", "profile", "pathId"}, {"preferred"});
        id(route.at("request"));
        require(!route.contains("preferred") || route.at("preferred").is_boolean(), "/catalog/routes/preferred", "Expected a boolean route preference.");
        for (const auto &stop : array(route.at("from"), 2)) id(stop);
        for (const auto &stop : array(route.at("to"), 2)) id(stop);
        for (const auto *key : {"profile", "pathId"}) if (route.contains(key)) id(route.at(key));
      }
    }
    require(puzzle_->bindings.movements.size() == puzzle_->bindings.choice_slots.size(),
      "/program/actions", "Controller support requires contiguous direct-choice action scopes.", "controls.unsupported");
    auto result = std::shared_ptr<Execution>(new Execution(*this));
    result->controls_ = ControlCatalog::compile(*definition_, puzzle_->bindings, catalog);
    return result;
  } catch (const Json::exception &error) { throw DiagnosticError("controls.invalid", "/", error.what()); }
}
std::string validate_runtime_json(const std::string &artifact, const std::string &controls) {
  try {
    const auto execution = runtime_execution(artifact, controls);
    return Json{{"status", "Valid"}, {"executionDigest", execution->definition()->digest}}.dump();
  } catch (const std::exception &error) { return diagnostic_result(error).dump(); }
}
} // namespace twisty
