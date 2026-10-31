#include "twisty/execution.hpp"
#include "artifact_internal.hpp"
#include <numeric>

namespace twisty {
std::shared_ptr<const Execution> Execution::with_interpretation(const std::string &text) const {
  if (!source_free()) throw DiagnosticError("interpretation.invalid", "/", "An artifact execution context is required.");
  auto result = std::shared_ptr<Execution>(new Execution(artifact_));
  try { result->bind_interpretation(artifact::parse_document(text)); }
  catch (const DiagnosticError &error) {
    if (error.code == "artifact.invalid" || error.code == "artifact.reference")
      throw DiagnosticError("interpretation.invalid", error.source, error.what());
    throw;
  } catch (const Json::exception &error) { throw DiagnosticError("interpretation.invalid", "/", error.what()); }
  return result;
}
void Execution::bind_interpretation(const Json &doc) {
  using namespace artifact::detail;
  fields(doc, {"kind", "contractVersion", "executionDigest", "interpretationDigest", "domains", "resources", "pieces", "placements", "certificates"});
  require(doc.at("kind") == "twisty-interpretation" && (doc.at("contractVersion") == 1 || doc.at("contractVersion") == 2), "/", "Unsupported interpretation contract.");
  require(doc.at("executionDigest") == definition_->digest, "/executionDigest", "Interpretation pins another executable.", "artifact.digest_mismatch");
  pin(doc, "interpretationDigest");
  interpretation_version_ = index(doc.at("contractVersion"));
  const auto &b = puzzle_->bindings;
  const auto &data = puzzle_->program.data();
  auto def = std::make_shared<Definition>(*definition_);
  def->domains.clear(); def->pieces.clear(); def->initial.placement_of.clear();
  def->cells = ids(doc.at("resources"));
  def->port_catalog = true; // Picking uses certified participation, not family spelling.
  std::vector<std::set<std::string>> ports;
  std::vector<Index> slots;
  std::set<std::string> domain_ids;
  std::set<Index> used_slots;
  for (const auto &d : array(doc.at("domains"), 4096)) {
    fields(d, {"id", "choiceSlot", "ports"});
    const auto name = id(d.at("id")); const auto slot = index(d.at("choiceSlot"));
    require(domain_ids.insert(name).second && slot < b.choice_slots.size() && used_slots.insert(slot).second,
      "/domains", "Domain IDs and choice-slot bindings must be unique and known.");
    const auto names = ids(d.at("ports"), 4096);
    ports.emplace_back(names.begin(), names.end()); slots.push_back(slot);
    Domain domain; domain.id = name; def->domains.push_back(std::move(domain));
  }
  require(!slots.empty(), "/domains", "An interpretation requires domains.");
  global_of_local_.assign(slots.size(), {});
  local_of_global_.assign(data.placement_count, 0);
  std::vector<Index> domain_of(data.placement_count);
  const auto &placements = array(doc.at("placements"));
  require(placements.size() == data.placement_count, "/placements", "Placement coverage differs.");
  for (Index q = 0; q < placements.size(); ++q) {
    const auto &entry = placements[q]; fields(entry, {"placement", "domain", "resources", "ports"});
    const auto domain = index(entry.at("domain"));
    require(index(entry.at("placement")) == q && domain < slots.size(), "/placements", "Expected ordered complete placement coverage.");
    domain_of[q] = domain;
    Placement place; place.key = b.placements[q];
    std::set<Index> footprint;
    for (auto resource : indices(entry.at("resources"))) {
      require(resource < def->cells.size() && footprint.insert(resource).second, "/placements/resources", "Unknown or duplicate resource.");
      place.footprint.push_back(def->cells[resource]);
    }
    for (const auto &port : array(entry.at("ports"), 4096)) {
      fields(port, {"id", "resource", "attachment"});
      const auto name = id(port.at("id")); const auto resource = index(port.at("resource"));
      require(ports[domain].contains(name) && footprint.contains(resource) &&
        place.ports.emplace(name, std::pair{def->cells[resource], id(port.at("attachment"))}).second,
        "/placements/ports", "Invalid or duplicate port correspondence.");
    }
    require(place.ports.size() == ports[domain].size(), "/placements/ports", "Incomplete port coverage.");
    auto &d = def->domains[domain]; local_of_global_[q] = static_cast<Index>(d.placements.size());
    d.by_key.emplace(place.key, static_cast<Index>(d.placements.size()));
    d.placements.push_back(std::move(place)); global_of_local_[domain].push_back(q);
  }
  const auto &pieces = array(doc.at("pieces"), 4096);
  require(pieces.size() == data.piece_count, "/pieces", "Piece coverage differs.");
  for (Index p = 0; p < pieces.size(); ++p) {
    const auto &entry = pieces[p]; fields(entry, {"piece", "domain", "portLabels"});
    const auto domain = index(entry.at("domain")), home = data.initial[p];
    require(index(entry.at("piece")) == p && domain < slots.size() && domain_of[home] == domain,
      "/pieces", "Piece domain/home correspondence differs.");
    Piece piece{b.pieces[p], def->domains[domain].id, domain, local_of_global_[home], {}};
    for (const auto &label : array(entry.at("portLabels"), 4096)) {
      fields(label, {"port", "material"}); const auto port = id(label.at("port"));
      require(ports[domain].contains(port) && piece.labels.emplace(port, id(label.at("material"))).second,
        "/pieces/portLabels", "Invalid piece port/material correspondence.");
    }
    require(piece.labels.size() == ports[domain].size(), "/pieces/portLabels", "Incomplete piece port labels.");
    def->initial.placement_of.push_back(piece.home); def->pieces.push_back(std::move(piece));
  }
  auto core_program = std::shared_ptr<const core::Program>(puzzle_, &puzzle_->program);
  auto local = std::make_shared<const std::vector<Index>>(local_of_global_);
  std::vector<std::shared_ptr<const std::vector<Index>>> globals;
  for (const auto &g : global_of_local_) globals.push_back(std::make_shared<const std::vector<Index>>(g));
  for (auto &op : def->operations) for (Index d = 0; d < slots.size(); ++d) {
    auto view = std::make_shared<OperationTransport>();
    view->program = core_program; view->placements = globals[d]; view->local = local; view->map = core::no_index;
    op.maps.push_back({view}); op.roles.push_back({view});
  }
  interpretation_slots_ = slots;
  choice_certificates_.resize(slots.size());
  std::vector<std::map<Index, std::shared_ptr<const OperationTransport>>> choice_views(slots.size());
  std::set<std::string> certificate_ids;
  std::set<std::pair<Index, Index>> request_domains;
  std::vector<bool> covered(data.actions.size(), false);
  for (const auto &cert : array(doc.at("certificates"))) {
    if (interpretation_version_ == 1)
      fields(cert, {"id", "domain", "request", "event", "transport", "actions", "unavailablePlacements"});
    else fields(cert, {"id", "domain", "choice", "request", "event", "transport", "actions", "unavailablePlacements"});
    const auto domain = index(cert.at("domain")), request = index(cert.at("request"));
    const auto event = index(cert.at("event")), map = index(cert.at("transport"));
    require(certificate_ids.insert(id(cert.at("id"))).second && domain < slots.size() && request < b.requests.size() &&
      event < b.events.size() && map < data.maps.size() && request_domains.emplace(request, domain).second,
      "/certificates", "Unknown, duplicate or ambiguous certificate.");
    const auto &r = b.requests[request];
    require(r.default_profile.has_value(), "/certificates/request", "A track certificate requires a concrete default profile.");
    const auto choice = r.profiles[*r.default_profile].choices[slots[domain]];
    if (interpretation_version_ == 2) require(index(cert.at("choice")) == choice, "/certificates/choice", "The track request does not select its certified local choice.");
    Certificate certificate{domain, choice, request, event, {}};
    const auto actions = indices(cert.at("actions"));
    require(!actions.empty() || (interpretation_version_ == 2 &&
      std::none_of(def->pieces.begin(), def->pieces.end(), [&](const auto &p) { return p.domain == domain; })),
      "/certificates/actions", "Only a virtual domain without pieces may have an empty action certificate.");
    for (auto i : actions) {
      require(i < data.actions.size() && !covered[i], "/certificates/actions", "Unknown or multiply certified action.");
      covered[i] = true;
      const auto &a = data.actions[i]; const auto &slice = data.requests[request];
      require(i >= slice.first_action && i - slice.first_action < slice.action_count &&
        a.event == event && a.map == map && def->pieces[a.piece].domain == domain && a.selection >= 2,
        "/certificates/actions", "Certificate disagrees with its action or request scope.");
      certificate.pieces.push_back(a.piece);
      const auto &selector = data.predicates[a.selection - 2];
      require(selector.opcode == core::Opcode::in && selector.bank == core::Bank::choice &&
        selector.input == slots[domain] && data.finite_sets[selector.set] == std::vector<Index>{choice},
        "/certificates/actions", "Uniform certificate requires the exact direct-choice selector.");
    }
    auto view = std::make_shared<OperationTransport>();
    view->program = core_program; view->placements = globals[domain]; view->local = local; view->map = map;
    std::set<Index> unavailable;
    for (auto q : indices(cert.at("unavailablePlacements"))) {
      require(q < data.placement_count && domain_of[q] == domain && unavailable.insert(q).second && !puzzle_->program.transport(map, q),
        "/certificates/unavailablePlacements", "Unavailable sections must be unique nonparticipants in the domain.");
      view->unavailable.push_back(local_of_global_[q]);
    }
    std::sort(view->unavailable.begin(), view->unavailable.end());
    for (auto q : global_of_local_[domain]) if (const auto to = puzzle_->program.transport(map, q))
      require(domain_of[*to] == domain, "/certificates/transport", "Transport leaves its declared domain.");
    std::sort(certificate.pieces.begin(), certificate.pieces.end());
    require(std::adjacent_find(certificate.pieces.begin(), certificate.pieces.end()) == certificate.pieces.end(),
      "/certificates/actions", "A local certificate repeats a piece.");
    require(choice_certificates_[domain].emplace(choice, static_cast<Index>(certificates_.size())).second,
      "/certificates/choice", "A local choice has ambiguous certificates.", "interpretation.ambiguous");
    certificates_.push_back(std::move(certificate));
    choice_views[domain].emplace(choice, view);
    if (interpretation_version_ == 1) {
      def->operations[request].maps[domain] = {view}; def->operations[request].roles[domain] = {view};
      require(event_operations_.emplace(std::pair{domain, b.events[event]}, request).second,
        "/certificates/event", "Uniform event mapping is ambiguous.", "interpretation.ambiguous");
    }
  }
  require(std::all_of(covered.begin(), covered.end(), [](bool x) { return x; }), "/certificates", "Every executable action needs a certificate.");
  if (interpretation_version_ == 2) for (Index request = 0; request < b.requests.size(); ++request) {
    const auto &r = b.requests[request];
    require(r.default_profile.has_value(), "/certificates/request", "Backend tracks require concrete default profiles.");
    const auto &choices = r.profiles[*r.default_profile].choices;
    for (Index domain = 0; domain < slots.size(); ++domain)
      if (const auto found = choice_views[domain].find(choices[slots[domain]]); found != choice_views[domain].end()) {
        def->operations[request].maps[domain] = {found->second};
        def->operations[request].roles[domain] = {found->second};
      }
  }
  interpretation_digest_ = doc.at("interpretationDigest");
  definition_ = std::move(def);
}
} // namespace twisty
