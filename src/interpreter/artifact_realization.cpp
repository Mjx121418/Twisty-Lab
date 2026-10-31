#include "twisty/contracts.hpp"
#include "artifact_realization.hpp"
#include "../artifact_internal.hpp"
#include "backend_registry.hpp"
#include <cctype>

namespace twisty::detail {
void remap_realization_requests(Json &geometry, const std::map<std::string, std::string> &names) {
  const auto name = [&](const Json &value) -> std::string {
    const auto id = artifact::detail::id(value);
    const auto found = names.find(id);
    if (found == names.end()) throw DiagnosticError("realization.reference", "/paths", "Unknown request path identity.");
    return found->second;
  };
  const auto table = [&](Json &value) {
    artifact::detail::require(value.is_object(), "/paths", "Expected a request path table.");
    Json result = Json::object();
    for (auto &[key, entry] : value.items()) {
      if (entry.is_object() && entry.value("kind", Json{}) == "transport-regions")
        for (auto &region : entry.at("regions")) region["request"] = name(region.at("request"));
      result[name(key)] = std::move(entry);
    }
    value = std::move(result);
  };
  for (auto key : {"operationTracks", "explicitCubeTracks"}) if (geometry.contains(key)) table(geometry[key]);
  if (geometry.contains("operationTrackTables"))
    for (auto &value : geometry["operationTrackTables"]) table(value);
  if (geometry.contains("componentsByDomain"))
    for (auto &domain : geometry["componentsByDomain"]) for (auto &component : domain)
      if (component.at("operationTracks").is_object()) table(component["operationTracks"]);
  if (geometry.contains("controlLayout") && geometry["controlLayout"].at("version") == 2)
    for (auto &group : geometry["controlLayout"]["groups"]) for (auto &category : group["categories"])
      for (auto &axis : category["axes"]) for (auto &turn : axis["turns"])
        turn["operation"] = name(turn.at("operation"));
}
} // namespace twisty::detail

namespace twisty {
GeometryDefinition::GeometryDefinition(std::shared_ptr<const Execution> execution,
    const std::string &interpretation_text, const std::string &envelope_text, const std::string &payload_text) {
  using namespace artifact::detail;
  if (!execution) throw DiagnosticError("realization.definition", "/", "An execution context is required.");
  auto interpreted = execution->with_interpretation(interpretation_text);
  const auto envelope = artifact::parse_document(envelope_text);
  fields(envelope, {"kind", "contractVersion", "id", "interpretationDigest", "realizationDigest", "requiredCapabilities", "backend", "payload", "eventTracks"});
  require(envelope.at("kind") == contracts::realization_kind && envelope.at("contractVersion") == contracts::realization_version,
    "/", "Unsupported realization envelope.", "realization.unsupported");
  id(envelope.at("id"));
  require(envelope.at("interpretationDigest") == interpreted->interpretation_digest(),
    "/interpretationDigest", "Realization pins another interpretation.", "realization.digest_mismatch");
  pin(envelope, "realizationDigest");
  fields(envelope.at("backend"), {"id", "version"});
  const auto backend = id(envelope.at("backend").at("id"));
  const auto &descriptor = detail::geometry_backend(backend);
  require(descriptor.envelope && envelope.at("backend").at("version") == descriptor.version,
    "/backend", "Unsupported geometric backend/version.", "realization.unsupported");
  ids(envelope.at("requiredCapabilities"), 64);
  detail::validate_backend_capabilities(descriptor, envelope.at("requiredCapabilities"));
  const auto &resource = envelope.at("payload"); fields(resource, {"path", "sha256"});
  require(resource.at("path").is_string(), "/payload/path", "Expected a relative payload resource path.");
  const auto path = resource.at("path").get<std::string>();
  require(!path.empty() && path.size() <= 1024 && path.front() != '/' && path.back() != '/', "/payload/path", "Invalid relative payload resource path.");
  std::size_t begin = 0;
  while (begin < path.size()) {
    const auto end = path.find('/', begin); const auto part = path.substr(begin, end - begin);
    require(!part.empty() && part != "." && part != ".." && std::all_of(part.begin(), part.end(), [](unsigned char c) {
      return c < 128 && (std::isalnum(c) || c == '.' || c == '_' || c == '-');
    }), "/payload/path", "Invalid relative payload resource path.");
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  require(payload_text.size() <= 64 * 1024 * 1024, "/payload", "Payload exceeds 64 MiB.", "artifact.limit");
  require(resource.at("sha256") == sha256(payload_text), "/payload/sha256", "Payload bytes do not match their content hash.", "realization.payload_mismatch");
  auto payload = parse_geometry_document(payload_text);
  fields(payload, {"payloadVersion", "requestBindings", "pieceKinds", "geometry"});
  require(payload.at("payloadVersion") == contracts::backend_payload_version, "/payloadVersion", "Unsupported backend payload version.", "realization.unsupported");
  auto context = std::shared_ptr<Execution>(new Execution(*interpreted));
  auto definition = std::make_shared<Definition>(*interpreted->definition());
  const auto &requests = array(payload.at("requestBindings"), 4096);
  require(requests.size() == definition->operations.size(), "/requestBindings", "Expected complete ordered request bindings.");
  std::map<std::string, std::string> names;
  std::vector<std::string> paths;
  for (Index r = 0; r < requests.size(); ++r) {
    const auto &binding = requests[r]; fields(binding, {"pathId", "group", "resources"});
    const auto path_id = id(binding.at("pathId")); paths.push_back(path_id);
    require(names.emplace(path_id, definition->operations[r].id).second, "/requestBindings/pathId", "Duplicate backend path identity.");
    definition->operations[r].family = id(binding.at("group"));
    std::set<Index> selected;
    for (auto resource_index : artifact::detail::indices(binding.at("resources"))) {
      require(resource_index < definition->cells.size() && selected.insert(resource_index).second, "/requestBindings/resources", "Unknown or duplicate selected resource.");
      definition->operations[r].cells.push_back(definition->cells[resource_index]);
    }
  }
  const auto &tracks = array(envelope.at("eventTracks"));
  require(tracks.size() == context->certificates_.size(), "/eventTracks", "Event tracks must cover every certificate.");
  std::vector<bool> covered(tracks.size(), false);
  for (const auto &track : tracks) {
    fields(track, {"certificate", "pathId"}); const auto cert = index(track.at("certificate"));
    require(cert < covered.size() && !covered[cert], "/eventTracks", "Unknown or duplicate certificate path."); covered[cert] = true;
    require(id(track.at("pathId")) == paths.at(context->certificates_[cert].request),
      "/eventTracks/pathId", "Event track disagrees with its certified backend request.", "realization.reference");
  }
  const auto &kinds = array(payload.at("pieceKinds"), 4096);
  require(kinds.size() == definition->pieces.size(), "/pieceKinds", "Expected one geometric kind per core piece.");
  for (Index p = 0; p < kinds.size(); ++p) {
    require(kinds[p] == "rigid" || kinds[p] == "cube-bandage", "/pieceKinds", "Unknown geometric piece kind.");
    require(kinds[p] == "rigid" || descriptor.cube, "/pieceKinds", "A compound cube kind requires a cube backend.");
    definition->pieces[p].type = kinds[p] == "cube-bandage" ? "bandage" : "rigid";
  }
  auto geometry = std::move(payload.at("geometry"));
  fields(geometry, {"schemaVersion", "id", "kind", "compatibleDefinitionDigest", "requiredCapabilities"},
    {"explicitCubeTracks", "angularSegments", "diskAngleDegrees", "fidelity", "portInset", "radialSegments", "radius", "surfaceLift",
     "angularStops", "bodyInset", "controlLayout", "modelByDomain", "models", "motionFamilies", "operationTracks", "placementFrames",
     "scale", "stickerInset", "stickerLift", "assets", "componentsByDomain", "modelProvenance", "operationTrackTables", "rotationTracks",
     "flatSurface", "cellShape", "cutDepth", "domains", "pieceInset"});
  require(geometry.at("kind") == backend && geometry.at("requiredCapabilities") == envelope.at("requiredCapabilities"),
    "/geometry", "Backend kind/capabilities differ from the envelope.");
  if (descriptor.cube) require(geometry.contains("explicitCubeTracks"), "/explicitCubeTracks", "Cube artifact paths require explicit axes and signed angles.");
  require(!geometry.contains("controlLayout") || geometry.at("controlLayout").at("version") == 2,
    "/controlLayout", "Controller routes require a separate resolved controller contract.", "realization.capability");
  detail::remap_realization_requests(geometry, names);
  definition->port_catalog = !descriptor.cube;
  context->definition_ = std::move(definition);
  const auto digest = envelope.at("realizationDigest").get<std::string>();
  program_ = detail::compile_geometry_backend(context, geometry, digest);
}
} // namespace twisty
