#include "twisty/artifact.hpp"
#include "twisty/session.hpp"
#include "twisty/geometry.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <numbers>

using namespace twisty;
namespace {
int checks = 0;
std::string read(const std::string &name) {
  std::ifstream file(std::string(PROJECT_ROOT) + "/" + name);
  if (!file) throw std::runtime_error("Missing artifact fixture: " + name);
  std::ostringstream s; s << file.rdbuf(); return s.str();
}
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
template <class F> void reject(F f, const std::string &code = {}) {
  try { f(); }
  catch (const DiagnosticError &error) { check(code.empty() || error.code == code, ("Wrong diagnostic: " + error.code).c_str()); return; }
  throw std::runtime_error("Invalid artifact/interpretation was accepted.");
}
void model(const std::string &name, const std::string &algorithm) {
  const auto directory = "examples/artifact/data/" + name + "/";
  const auto text = read(directory + "abstract.json");
  const auto manifest_text = read(directory + "interpretation.json");
  const auto realization_text = read(directory + "realization.json");
  auto doc = Json::parse(text), manifest = Json::parse(manifest_text), realization = Json::parse(realization_text);
  auto execution = Execution::from_artifact(text);
  Session session(execution); session.lightweight_results(true);
  const auto origin = session.state_json();
  check(!session.definition()->canonical.contains("source"), "Artifact retained authoring source");
  check(session.snapshot(SnapshotDetail::StateOnly).contains("executionDigest"), "Execution identity missing");
  check(session.execute("U", "999").at("status") == "StaleRevision" && session.state_json() == origin, "Stale artifact mutation");
  if (name == "bandaged") check(session.execute("R", session.revision()).at("status") == "Blocked" && session.state_json() == origin, "Bandage guard changed");
  auto geometry = std::make_shared<GeometryDefinition>(execution, manifest_text, realization_text);
  check(Json::parse(geometry->scene_json()).at("realizationDigest") == sha256(realization_text), "Realization content identity differs");
  auto result = session.run(algorithm, "transactional", session.revision());
  check(result.at("status") == "Committed", "Artifact algorithm failed");
  std::size_t self_loops = 0;
  for (const auto &record : result.at("transitions")) {
    check(record.at("witnessVersion") == 2 && record.contains("executionDigest"), "Wrong artifact witness contract");
    for (const auto &a : record.at("pieceActions")) if (a.at("from") == a.at("to")) ++self_loops;
    auto animation = geometry->animate(record.dump());
    check(animation.evaluate(0).isometries() == geometry->frame(record.at("beforeState").dump()).isometries(), "Source poses differ");
    check(animation.evaluate(1).isometries() == geometry->frame(record.at("afterState").dump()).isometries(), "Target poses differ");
    const auto middle = animation.evaluate(.5).isometries(); animation.evaluate(.9);
    check(animation.evaluate(.5).isometries() == middle, "Artifact animation is not seekable");
  }
  check(self_loops > 0, "Self-loop participants were lost");
  auto saved = session.save(); check(saved.at("schemaVersion") == 3, "Artifact save did not use schema 3");
  check(session.undo(session.revision()).at("status") == "Committed", "Artifact undo failed");
  auto tail = session.save();
  Session restored(execution); restored.lightweight_results(true);
  check(restored.load(tail).at("status") == "Loaded" && restored.redo(restored.revision()).at("status") == "Committed", "Recorded redo tail failed");
  const auto before = session.snapshot(SnapshotDetail::StateOnly);
  auto bad_save = saved; bad_save["logical"]["executionDigest"] = std::string(64, '0');
  check(Json::parse(session.load_json(bad_save.dump())).at("status") == "Invalid" && session.snapshot(SnapshotDetail::StateOnly) == before, "Failed replay changed live state");
  bad_save = saved; bad_save["schemaVersion"] = 2;
  check(Json::parse(session.load_json(bad_save.dump())).at("status") == "Invalid", "Legacy save was silently migrated");
  auto record = result.at("transitions")[0]; record["request"].erase("choices");
  reject([&] { geometry->animate(record.dump()); }, "realization.witness");
  auto winding = realization;
  const auto operation = result.at("transitions")[0].at("request").at("operation").get<std::string>();
  winding["explicitCubeTracks"][operation]["angle"] =
    winding["explicitCubeTracks"][operation]["angle"].get<double>() + 2 * std::numbers::pi;
  GeometryDefinition alternate(execution, manifest_text, winding.dump());
  check(Json::parse(alternate.scene_json()).at("sceneId") != Json::parse(geometry->scene_json()).at("sceneId"), "Different paths reused scene identity");
  auto alternate_motion = alternate.animate(result.at("transitions")[0].dump());
  auto original_motion = geometry->animate(result.at("transitions")[0].dump());
  check(alternate_motion.evaluate(1).isometries() == original_motion.evaluate(1).isometries(), "Full winding changed endpoints");
  check(alternate_motion.evaluate(.5).isometries() != original_motion.evaluate(.5).isometries(), "Full winding was discarded");
  const auto held = geometry->animate(result.at("transitions")[0].dump());
  const auto expected = held.evaluate(.5).isometries(); geometry.reset();
  check(held.evaluate(.5).isometries() == expected, "Animation lost immutable artifact lifetime");
  auto broken = [&](auto edit, const std::string &code = {}) {
    auto copy = doc; edit(copy); artifact::seal(copy, "executionDigest");
    reject([&] { artifact::Artifact bad(copy.dump()); }, code);
  };
  broken([](Json &j) { j["source"] = Json::object(); });
  broken([](Json &j) { j["requiredCapabilities"] = {"unknown-extension"}; }, "artifact.capability");
  broken([](Json &j) { j["artifactVersion"] = 2; }, "artifact.unsupported");
  broken([](Json &j) { j["program"]["requestPrograms"][0]["firstAction"] = 4294967294u; });
  broken([](Json &j) { j["bindings"]["requests"][0]["defaultProfile"] = 999; }, "artifact.reference");
  broken([](Json &j) { j["bindings"]["requests"][0]["profiles"][0]["choices"][0] = 999; }, "artifact.reference");
  broken([](Json &j) { j["bindings"]["requests"][0]["profiles"][0]["whenRoot"] = j["program"]["actionRecords"][0]["selectionRoot"]; }, "artifact.reference");
  broken([](Json &j) { j["session"]["requestInverses"][0] = 0; }, "artifact.reference");
  broken([](Json &j) { j["bindings"]["pieces"][0] = j["bindings"]["pieces"][1]; });
  auto invalid = doc; invalid["executionDigest"] = std::string(64, '0');
  reject([&] { artifact::Artifact bad(invalid.dump()); }, "artifact.digest_mismatch");
  reject([&] { artifact::Artifact bad("{\"kind\":\"twisty-abstract-artifact\"," + text.substr(text.find('{') + 1)); }, "artifact.invalid");
  reject([&] { artifact::parse_document(std::string(66, '[') + "0" + std::string(66, ']')); }, "artifact.limit");
  invalid = doc; invalid["program"]["actionRecords"][0]["piece"] = 0.5;
  reject([&] { artifact::Artifact bad(invalid.dump()); });
  auto bare = doc; bare.erase("bindings"); bare.erase("session"); artifact::seal(bare, "executionDigest");
  artifact::Artifact core_only(bare.dump());
  check(core_only.compiled()->program.data().piece_count > 0 && !core_only.has_session(), "Bare core loading requires a session or geometry");
  reject([&] { Execution::from_artifact(bare.dump()); }, "artifact.capability");
  auto wrong_manifest = [&](auto edit) {
    auto m = manifest; edit(m); artifact::seal(m, "interpretationDigest");
    reject([&] { execution->with_interpretation(m.dump()); });
  };
  wrong_manifest([](Json &m) { m["certificates"].erase(m["certificates"].begin()); });
  wrong_manifest([](Json &m) { m["certificates"][0]["actions"].push_back(m["certificates"][0]["actions"][0]); });
  wrong_manifest([](Json &m) { m["certificates"][0]["event"] = 999; });
  wrong_manifest([](Json &m) { m["certificates"][0]["request"] = 1; });
  wrong_manifest([](Json &m) { m["resources"].erase(m["resources"].end() - 1); });
  auto merged_events = doc;
  for (auto &action : merged_events["program"]["actionRecords"]) action["event"] = 0;
  artifact::seal(merged_events, "executionDigest");
  auto ambiguous = manifest; ambiguous["executionDigest"] = merged_events["executionDigest"];
  for (auto &certificate : ambiguous["certificates"]) certificate["event"] = 0;
  artifact::seal(ambiguous, "interpretationDigest");
  reject([&] { Execution::from_artifact(merged_events.dump())->with_interpretation(ambiguous.dump()); }, "interpretation.ambiguous");
  auto wrong_realization = realization;
  wrong_realization["id"] = std::string(257, 'x');
  reject([&] { GeometryDefinition bad(execution, manifest_text, wrong_realization.dump()); }, "artifact.invalid");
  wrong_realization = realization;
  wrong_realization["compatibleInterpretationDigest"] = std::string(64, '0');
  reject([&] { GeometryDefinition bad(execution, manifest_text, wrong_realization.dump()); }, "realization.digest_mismatch");
  wrong_realization = realization;
  wrong_realization["explicitCubeTracks"].begin().value()["angle"] = 0;
  reject([&] { GeometryDefinition bad(execution, manifest_text, wrong_realization.dump()); }, "realization.unsupported_transition");
  check(session.snapshot(SnapshotDetail::StateOnly) == before, "Invalid presentation changed live session");
}
void envelope_model(const std::string &name) {
  const auto dir = "examples/artifact/data/" + name + "/";
  auto doc = Json::parse(read(dir + "abstract.json"));
  auto manifest = Json::parse(read(dir + "interpretation.json"));
  const auto old_realization = Json::parse(read(dir + "realization.json"));
  auto geometry = old_realization;
  geometry["kind"] = "cube-euclidean"; geometry.erase("compatibleInterpretationDigest");
  manifest["contractVersion"] = 2;
  for (auto &c : manifest["certificates"]) {
    const auto r = c["request"].get<Index>(), d = c["domain"].get<Index>();
    const auto slot = manifest["domains"][d]["choiceSlot"].get<Index>();
    const auto &request = doc["bindings"]["requests"][r];
    c["choice"] = request["profiles"][request["defaultProfile"].get<Index>()]["choices"][slot];
  }
  // Event names may repeat across distinct local choices; completed choices
  // must resolve the correct signed path, including inverse requests.
  for (auto &a : doc["program"]["actionRecords"]) a["event"] = 0;
  artifact::seal(doc, "executionDigest");
  for (auto &c : manifest["certificates"]) c["event"] = 0;
  manifest["executionDigest"] = doc["executionDigest"];
  for (Index d = 0; d < manifest["domains"].size(); ++d) manifest["domains"][d]["id"] = "scope-" + std::to_string(d);
  artifact::seal(manifest, "interpretationDigest");
  geometry["compatibleDefinitionDigest"] = doc["executionDigest"];
  Json bindings = Json::array(), kinds = Json::array(), paths = Json::object(), event_tracks = Json::array();
  for (Index r = 0; r < doc["bindings"]["requests"].size(); ++r) {
    const auto path = "path-" + std::to_string(r), id = doc["bindings"]["requests"][r]["id"].get<std::string>();
    bindings.push_back({{"pathId", path}, {"group", "arbitrary"}, {"resources", Json::array()}});
    paths[path] = geometry["explicitCubeTracks"][id];
  }
  geometry["explicitCubeTracks"] = paths;
  const auto original_manifest = Json::parse(read(dir + "interpretation.json"));
  for (const auto &piece : original_manifest["pieces"])
    kinds.push_back(original_manifest["domains"][piece["domain"].get<Index>()]["id"] == "bandage" ? "cube-bandage" : "rigid");
  for (Index c = 0; c < manifest["certificates"].size(); ++c)
    event_tracks.push_back({{"certificate", c}, {"pathId", bindings[manifest["certificates"][c]["request"].get<Index>()]["pathId"]}});
  Json payload{{"payloadVersion", 1}, {"requestBindings", bindings}, {"pieceKinds", kinds}, {"geometry", geometry}};
  Json envelope{{"kind", "twisty-realization"}, {"contractVersion", 1}, {"id", "explicit-cube"},
    {"interpretationDigest", manifest["interpretationDigest"]}, {"requiredCapabilities", geometry["requiredCapabilities"]},
    {"backend", {{"id", "cube-euclidean"}, {"version", 1}}},
    {"eventTracks", event_tracks}, {"payload", {{"path", "geometry.json"}, {"sha256", sha256(payload.dump())}}}};
  artifact::seal(envelope, "realizationDigest");
  auto execution = Execution::from_artifact(doc.dump());
  Session session(execution); session.lightweight_results(true);
  GeometryDefinition drawing(execution, manifest.dump(), envelope.dump(), payload.dump());
  const auto result = session.run(name == "bandaged" ? "U R R' U'" : "R U R' U'", "transactional", session.revision());
  check(result["status"] == "Committed", "Contextual event algorithm failed");
  for (const auto &record : result["transitions"]) {
    auto animation = drawing.animate(record.dump());
    check(animation.evaluate(0).isometries() == drawing.frame(record["beforeState"].dump()).isometries(), "Contextual source differs");
    check(animation.evaluate(1).isometries() == drawing.frame(record["afterState"].dump()).isometries(), "Contextual target differs");
    const auto midpoint = animation.evaluate(.5).isometries(); animation.evaluate(.1);
    check(animation.evaluate(.5).isometries() == midpoint, "Contextual seek differs");
    auto forged = record; forged["pieceActions"][0]["event"] = "forged";
    reject([&] { drawing.animate(forged.dump()); }, "realization.witness");
  }
  const auto before = session.state_snapshot_json();
  auto invalid = [&](auto edit, const std::string &code = {}) {
    auto e = envelope, p = payload; edit(e, p); e["payload"]["sha256"] = sha256(p.dump()); artifact::seal(e, "realizationDigest");
    reject([&] { GeometryDefinition bad(execution, manifest.dump(), e.dump(), p.dump()); }, code);
  };
  invalid([](Json &e, Json &) { e["backend"]["version"] = 99; }, "realization.unsupported");
  invalid([](Json &e, Json &) { e["requiredCapabilities"].push_back("unknown"); }, "realization.capability");
  invalid([](Json &e, Json &) { e["eventTracks"].erase(e["eventTracks"].begin()); });
  invalid([](Json &e, Json &) { e["eventTracks"][0]["pathId"] = "missing"; }, "realization.reference");
  invalid([](Json &e, Json &) { e["payload"]["path"] = "../escape.json"; });
  invalid([](Json &, Json &p) { p["geometry"]["explicitCubeTracks"].begin().value()["angle"] = 0; }, "realization.unsupported_transition");
  invalid([](Json &, Json &p) { p["geometry"]["explicitCubeTracks"].erase(p["geometry"]["explicitCubeTracks"].begin()); });
  reject([&] { GeometryDefinition bad(execution, manifest.dump(), envelope.dump(), payload.dump() + " "); }, "realization.payload_mismatch");
  check(session.state_snapshot_json() == before, "Failed geometric binding changed live session");
}

} // namespace
int main() {
  try {
    for (const auto &v : Json::parse(read("docs/contracts/hash-vectors.json"))) {
      check(artifact::canonical_json(v.at("value")) == v.at("canonical"), "Portable canonical bytes differ");
      check(sha256(artifact::canonical_json(v.at("value"))) == v.at("sha256"), "Portable canonical SHA-256 differs");
    }
    reject([] { artifact::parse_document("{\"items\":[{\"x\":0},{\"x\":1,\"x\":2}]}"); }, "artifact.invalid");
    core::ProgramData raw;
    raw.piece_count = 1; raw.placement_count = 2; raw.initial = {0};
    raw.maps = {{{0, 1}, {1, 0}}}; raw.actions = {{0, 0, core::true_ref, 7}};
    raw.requests = {{{}, 0, 1, core::true_ref}};
    Json bare{{"kind", "twisty-abstract-artifact"}, {"artifactVersion", 1}, {"requiredCapabilities", Json::array()},
      {"program", artifact::encode_program(raw)}};
    artifact::seal(bare, "executionDigest");
    artifact::Artifact numeric(bare.dump());
    check(numeric.compiled()->program.plan(0, raw.initial, {}).transition->after == core::State{1}, "Bare numeric program changed");
    auto grouped = bare; grouped["requiredCapabilities"] = {"grouped-action-records-v1"};
    grouped["program"] = artifact::encode_program(raw, true); artifact::seal(grouped, "executionDigest");
    artifact::Artifact compact_actions(grouped.dump());
    check(compact_actions.compiled()->program.plan(0, raw.initial, {}).transition->after == core::State{1}, "Grouped action semantics differ");
    auto malformed_group = grouped; malformed_group["program"]["actionGroups"][0]["pieces"] = Json::array();
    artifact::seal(malformed_group, "executionDigest"); reject([&] { artifact::Artifact invalid(malformed_group.dump()); });
    malformed_group = grouped; malformed_group["requiredCapabilities"] = Json::array();
    artifact::seal(malformed_group, "executionDigest"); reject([&] { artifact::Artifact invalid(malformed_group.dump()); });
    auto bad = bare; bad["program"]["compact"]["maps"][0]["rows"] = {{0, 1}, {1, 1}};
    artifact::seal(bad, "executionDigest"); reject([&] { artifact::Artifact invalid(bad.dump()); });
    bad = bare; bad["program"]["compact"]["groups"] = Json::array({{{"order", 2}, {"products", {0, 1, 1, 1}}, {"inverses", {0, 1}}}});
    artifact::seal(bad, "executionDigest"); reject([&] { artifact::Artifact invalid(bad.dump()); });
    bad = bare; bad["program"]["compact"]["spaces"] = Json::array({{{"size", 2}}, {{"size", 3}}});
    bad["program"]["compact"]["positionMaps"] = Json::array({{{"sourceSpace", 0}, {"targetSpace", 1}, {"rows", {{0, 0}, {1, 3}}}}});
    artifact::seal(bad, "executionDigest"); reject([&] { artifact::Artifact invalid(bad.dump()); });
    bad = bare; bad["program"]["predicateNodes"] = Json::array({{{"op", "RANGE"}, {"min", 0}, {"max", 1}, {"firstChild", 0}, {"childCount", 1}}});
    bad["program"]["childReferences"] = {2}; artifact::seal(bad, "executionDigest");
    reject([&] { artifact::Artifact invalid(bad.dump()); });
    model("cube3", "R U R' U'"); model("bandaged", "U R R' U'");
    envelope_model("cube3"); envelope_model("bandaged");
    std::cout << checks << " source-free artifact checks passed.\n";
  } catch (const std::exception &error) { std::cerr << diagnostic_result(error).dump() << '\n'; return 1; }
}
