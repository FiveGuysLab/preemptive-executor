#ifndef CHAIN_YAML_PARSER_HPP
#define CHAIN_YAML_PARSER_HPP

#include <yaml-cpp/yaml.h>

#include <string>

namespace preemptive_executor {

struct userChain {
  std::string chain_name;
  std::vector<std::string> callbacks;
  std::uint32_t deadline;
  std::uint32_t period;
};

class ChainYamlParser {
 public:
  ChainYamlParser() = default;
  ~ChainYamlParser() = default;

  void load_yaml_file(const std::string& yaml_file);
  bool parse();
  const std::unordered_map<std::string, userChain>& get_user_chains() const { return user_chains; }

 private:
  static constexpr const char* CALLBACKS_KEY = "callbacks";
  static constexpr const char* DEADLINE_KEY = "deadline";
  static constexpr const char* PERIOD_KEY = "period";
  std::string yaml_file;
  YAML::Node yaml_node;
  std::unordered_map<std::string, userChain> user_chains;
};
}  // namespace preemptive_executor
#endif