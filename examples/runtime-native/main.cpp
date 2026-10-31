#include <twisty/runtime_api.hpp>
#include <twisty/session.hpp>
#include <twisty/controller.hpp>
#include <twisty/geometry.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
std::string read(const char *path) {
  std::ifstream file(path); if(!file) throw std::runtime_error("Missing runtime document.");
  std::ostringstream text; text<<file.rdbuf(); return text.str();
}
int main(int argc,char **argv) {
  try {
    if(argc!=7) throw std::runtime_error("Expected artifact, controls, interpretation, envelope, payload and algorithm.");
    const auto execution=twisty::runtime_execution(read(argv[1]),read(argv[2]));
    twisty::Session session(execution); session.lightweight_results(true);
    twisty::Controller controller(execution);
    twisty::GeometryDefinition geometry(execution,read(argv[3]),read(argv[4]),read(argv[5]));
    auto result=session.run(argv[6],"transactional",session.revision());
    if(result.at("status")!="Committed") throw std::runtime_error(result.dump());
    twisty::Json poses=twisty::Json::array();
    for(const auto &record:result.at("transitions")) {
      const auto animation=geometry.animate(record.dump());
      twisty::Json samples=twisty::Json::array();
      for(const double progress:{0.0,0.5,1.0}) samples.push_back(animation.evaluate(progress).isometries());
      poses.push_back(std::move(samples));
    }
    const auto frame=geometry.frame(session.state_json());
    std::cout<<twisty::Json{{"snapshot",session.snapshot(twisty::SnapshotDetail::StateOnly)},
      {"result",result},{"controls",controller.catalog()},{"parts",frame.isometries().size()/16},
      {"save",session.save()},{"poses",poses}}.dump()<<'\n';
  } catch(const std::exception &error) {std::cerr<<twisty::diagnostic_result(error).dump()<<'\n';return 1;}
}
