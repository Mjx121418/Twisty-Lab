#include "twisty/authoring/runtime.hpp"
#include "twisty/authoring/artifact.hpp"
#include "twisty/authoring/realization.hpp"
#include "twisty/controller.hpp"
#include "twisty/artifact.hpp"
#include "twisty/runtime_api.hpp"
#include "twisty/session.hpp"
#include <algorithm>

namespace twisty::language {
Json runtime_artifacts(const std::string &source, const std::string &realizations_text) {
  const auto execution = Execution::from_source(source);
  const auto abstract = execution->artifact_json();
  const auto &definition = *execution->definition();
  const auto &canonical = definition.canonical;
  auto catalog = execution->controls() ? execution->controls()->json()
    : Json{{"version", 1}, {"drivers", Json::array()}, {"families", Json::array()}};
  // Authored phase readers are local to a piece domain. Runtime readers use
  // the executable's global symbolic placement catalog, without geometry.
  const auto &bindings = execution->compiled_puzzle()->bindings;
  for (auto &driver : catalog["drivers"]) {
    const auto piece = std::find_if(definition.pieces.begin(), definition.pieces.end(),
      [&](const auto &p) { return p.id == driver.at("piece").get<std::string>(); });
    if (piece == definition.pieces.end()) throw DiagnosticError("controls.piece", "/drivers", "Unknown phase reader piece.");
    const auto &domain = definition.domains[piece->domain];
    Json reader = Json::array();
    for (const auto &[placement, stop] : driver.at("placements").items()) {
      const auto global = std::find(bindings.placements.begin(), bindings.placements.end(), domain.id + "/" + placement);
      const auto target = std::find(driver.at("stops").begin(), driver.at("stops").end(), stop);
      reader.push_back({{"placement", global - bindings.placements.begin()}, {"stop", target - driver.at("stops").begin()}});
    }
    driver["piece"] = std::find(bindings.pieces.begin(), bindings.pieces.end(), piece->id) - bindings.pieces.begin();
    driver.erase("placements"); driver["reader"] = std::move(reader);
  }
  for (auto &family : catalog["families"]) for (auto &route : family["routes"]) {
    if (!route.contains("pathId")) route["pathId"] = route.at("request");
    if (!route.contains("profile")) {
      const auto &requests = execution->compiled_puzzle()->bindings.requests;
      const auto request = std::find_if(requests.begin(), requests.end(), [&](const auto &r) { return r.id == route.at("request").get<std::string>(); });
      route["profile"] = request->profiles.at(*request->default_profile).id;
    }
  }
  Json controls{{"kind", "twisty-controls"}, {"contractVersion", 1},
    {"executionDigest", abstract.at("executionDigest")}, {"catalog", catalog}};
  artifact::seal(controls, "controlDigest");
  runtime_execution(abstract.dump(), controls.dump());
  Json presentation{{"kind", "twisty-presentation"}, {"contractVersion", 1},
    {"executionDigest", abstract.at("executionDigest")}, {"legacyDefinitionDigest", definition.digest}};
  for (const auto *key : {"puzzleId", "pieceTypes", "placementDomains", "pieces", "operations", "cells", "symmetry"})
    if (canonical.contains(key)) presentation[key] = canonical.at(key);
  for (auto &domain : presentation["placementDomains"])
    for (auto &placement : domain["placements"]) placement["key"] = domain.at("id").get<std::string>() + "/" + placement.at("key").get<std::string>();
  for (auto &piece : presentation["pieces"]) {
    const auto type = std::find_if(presentation["pieceTypes"].begin(), presentation["pieceTypes"].end(),
      [&](const auto &t) { return t.at("id") == piece.at("type"); });
    piece["homePlacement"] = type->at("placementDomainId").get<std::string>() + "/" + piece.at("homePlacement").get<std::string>();
  }
  Json result{{"abstract", abstract}, {"controls", controls}, {"presentation", presentation},
    {"interpretation", interpretation_artifact(*execution, abstract)}, {"views", Json::array()}};
  const auto realizations = Json::parse(realizations_text);
  if (!realizations.is_array() || realizations.size() > 64) throw DiagnosticError("authoring.realizations", "/", "Expected at most 64 views.");
  for (const auto &geometry : realizations) {
    const auto bundle = realization_artifacts(execution, geometry.dump());
    if (bundle.at("abstract") != abstract || bundle.at("interpretation") != result.at("interpretation"))
      throw DiagnosticError("authoring.context", "/", "Views disagree on execution/correspondence.");
    Json hint = Json::object();
    for (const auto *key : {"schemaVersion", "id", "kind", "requiredCapabilities", "controlLayout", "operationTracks", "rotationTracks", "motionFamilies", "cutDepth", "radius", "flatSurface"})
      if (geometry.contains(key)) hint[key] = geometry.at(key);
    hint["compatibleDefinitionDigest"] = abstract.at("executionDigest");
    result["views"].push_back({{"realization", bundle.at("realization")}, {"payload", bundle.at("payload")}, {"presentation", hint}});
  }
  return result;
}
std::string runtime_artifacts_json(const std::string &source, const std::string &realizations) {
  try { return runtime_artifacts(source, realizations).dump(); }
  catch (const std::exception &error) { return diagnostic_result(error).dump(); }
}
std::string migrate_runtime_session_json(const std::string &source, const std::string &text) {
  try {
    Json executable;
    std::map<std::string, std::string> prefixes;
    {
      const auto legacy = Execution::from_source(source);
      Session original(legacy);
      original.lightweight_results(true);
      const auto loaded = Json::parse(original.load_json(text));
      if (loaded.at("status") != "Loaded") return loaded.dump();
      executable = legacy->artifact_json();
      for (const auto &piece : legacy->definition()->pieces)
        prefixes[piece.id] = legacy->definition()->domains[piece.domain].id + "/";
    } // Release source graphs before loading the independent runtime program.
    auto document = Json::parse(text);
    const auto execution = Execution::from_artifact(executable.dump());
    executable = nullptr;
    const auto convert = [&](const Json &wire) {
      auto result = wire;
      for (const auto &[piece, prefix] : prefixes)
        result["placementOf"][piece] = prefix + wire.at("placementOf").at(piece).get<std::string>();
      return execution->decode(result);
    };
    auto &logical = document["logical"];
    auto state = convert(logical.at("originState"));
    auto cursor_state = state;
    logical["originState"] = encode_state(*execution->definition(), state);
    const auto cursor = logical.at("cursor").get<std::size_t>();
    std::size_t index = 0;
    for (auto &request : logical["requests"]) {
      const auto plan = execution->plan(state, request.at("operation"), request.at("choices"));
      if (!plan.transition) throw DiagnosticError("migration.replay", "/logical/requests", "Runtime replay differs from the verified source session.");
      state = plan.transition->after;
      request["afterStateDigest"] = state_digest(*execution->definition(), state);
      if (++index == cursor) cursor_state = state;
    }
    for (auto &checkpoint : logical["checkpoints"]) {
      const auto converted = convert(checkpoint.at("state"));
      checkpoint["state"] = encode_state(*execution->definition(), converted);
      checkpoint["stateDigest"] = state_digest(*execution->definition(), converted);
    }
    logical.erase("definitionDigest"); logical["executionDigest"] = execution->definition()->digest;
    logical["stateDigest"] = state_digest(*execution->definition(), cursor_state);
    document["schemaVersion"] = 3;
    Session verified(execution);
    verified.lightweight_results(true);
    const auto result = Json::parse(verified.load_json(document.dump()));
    if (result.at("status") != "Loaded") return result.dump();
    auto migrated = verified.save();
    migrated["presentation"] = document.value("presentation", Json::object());
    return Json{{"status", "Converted"}, {"sessionJSON", migrated.dump()}}.dump();
  } catch (const std::exception &error) { return diagnostic_result(error).dump(); }
}
} // namespace twisty::language
