#include "twisty/session.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace twisty;
namespace {
std::string read(const std::string &filename) {
  std::ifstream stream(filename);
  if (!stream)
    throw DiagnosticError("file.read", filename, "Cannot read file.");
  std::ostringstream output;
  output << stream.rdbuf();
  if (output.str().size() > 4 * 1024 * 1024)
    throw DiagnosticError("document.limit", filename, "Input is limited to 4 MiB.");
  return output.str();
}
void write(const std::string &filename, const Json &json) {
  std::ofstream stream(filename);
  if (!stream)
    throw DiagnosticError("file.write", filename, "Cannot write file.");
  stream << json.dump(2) << '\n';
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string command = argc > 1 ? argv[1] : "help";
    std::map<std::string, std::string> options;
    for (int i = 2; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--json")
        options[option] = "true";
      else if (option.starts_with("--") && i + 1 < argc)
        options[option] = argv[++i];
      else
        throw DiagnosticError("cli.argument", option, "Expected an option and value.");
    }
    auto option = [&](const std::string &name, const std::string &fallback = "") {
      return options.contains(name) ? options.at(name) : fallback;
    };
    if (command == "help" || command == "--help") {
      std::cout
          << "Twisty Lab — exact headless puzzle engine\n"
          << "Commands: compile, validate, inspect, run, scramble, replay, verify-fixtures\n"
          << "Options: --definition FILE --algorithm TEXT --policy interactive|transactional\n"
          << "         --seed N --length N --session FILE --state FILE --save FILE --output FILE --json\n";
      return 0;
    }
    Json result;
    if (command == "verify-fixtures") {
      const auto fixtures = Json::parse(read(option("--fixtures", "tests/fixtures/core.json")));
      Json results = Json::array();
      for (const auto &fixture : fixtures.at("cases")) {
        Session session(read(fixture.at("source")));
        if (fixture.at("definitionDigest") != session.definition()->digest)
          throw DiagnosticError("fixture.digest", fixture.at("name"),
                                "Fixture is pinned to different puzzle semantics.");
        const auto prefix = session.run(fixture.at("prefix"), "transactional", session.revision());
        if (prefix.at("status") != "Committed")
          throw DiagnosticError("fixture.prefix", fixture.at("name"), "Fixture prefix is blocked.");
        const auto outcome = session.execute(fixture.at("operation"), session.revision());
        bool pass = outcome.at("status") == fixture.at("expectedStatus");
        if (fixture.contains("reasonCode"))
          pass = pass && outcome.at("reasonCode") == fixture.at("reasonCode");
        if (fixture.contains("implicatedPiece"))
          pass =
              pass && std::find(outcome.at("implicatedPieces").begin(), outcome.at("implicatedPieces").end(),
                                fixture.at("implicatedPiece")) != outcome.at("implicatedPieces").end();
        if (fixture.contains("unchangedParticipant")) {
          bool present = false;
          for (const auto &action : outcome.at("transitions").at(0).at("pieceActions"))
            if (action.at("pieceId") == fixture.at("unchangedParticipant") &&
                action.at("from") == action.at("to"))
              present = true;
          pass = pass && present;
        }
        results.push_back({{"name", fixture.at("name")}, {"passed", pass}});
      }
      const bool all = std::all_of(results.begin(), results.end(),
                                   [](const Json &row) { return row.at("passed").get<bool>(); });
      result = {{"status", all ? "Passed" : "Failed"}, {"cases", results}};
    } else {
      const auto source = read(option("--definition", "packages/cube3/source.json"));
      if (command == "compile")
        result = compile_json(source);
      else {
        Session session(source);
        if (!option("--session").empty())
          session.load(Json::parse(read(option("--session"))));
        if (command == "inspect")
          result = {{"status", "Inspected"},
                    {"definition", Json::parse(session.definition_json())},
                    {"snapshot", session.snapshot()}};
        else if (command == "validate") {
          if (!option("--state").empty())
            decode_state(*session.definition(), Json::parse(read(option("--state"))));
          result = {{"status", "Valid"},
                    {"definitionDigest", session.definition()->digest},
                    {"reachability", "not-certified-for-imported-assignments"}};
        } else if (command == "run")
          result = session.run(option("--algorithm"), option("--policy", "interactive"), session.revision());
        else if (command == "scramble") {
          const auto seed = std::stoull(option("--seed", "42")),
                     length = std::stoull(option("--length", "25"));
          if (seed > UINT32_MAX || length > 1000)
            throw DiagnosticError("scramble.limit", "/",
                                  "Seed must fit uint32 and length must be at most 1000.");
          result = session.scramble(static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(length),
                                    session.revision());
        } else if (command == "replay") {
          if (option("--session").empty())
            throw DiagnosticError("cli.argument", "--session", "Replay requires a session file.");
          result = {{"status", "Replayed"}, {"snapshot", session.snapshot()}};
        } else
          throw DiagnosticError("cli.command", command, "Unknown command.");
        if (!option("--save").empty())
          write(option("--save"), session.save());
      }
    }
    if (!option("--output").empty())
      write(option("--output"),
            command == "compile" && result.at("status") == "Compiled" ? result.at("definition") : result);
    if (options.contains("--json"))
      std::cout << result.dump() << '\n';
    else
      std::cout << result.dump(2) << '\n';
    const auto status = result.at("status").get<std::string>();
    return status == "Invalid" || status == "Blocked" || status == "Failed" ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << diagnostic_result(error).dump(2) << '\n';
    return 2;
  }
}
