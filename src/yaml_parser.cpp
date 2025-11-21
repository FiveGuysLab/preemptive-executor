#include "preemptive_executor/yaml_parser.hpp"

#include <stdexcept>

#include "rclcpp/logging.hpp"

namespace preemptive_executor {
    void ChainYamlParser::load_yaml_file(const std::string& new_yaml_file) {
        try {
            yaml_node = YAML::LoadFile(new_yaml_file);
        } catch (const YAML::Exception& e) {
            RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "Failed to load YAML file: %s", e.what());
            throw std::runtime_error("Failed to load YAML file: " + std::string(e.what()));
        }
    }

    bool ChainYamlParser::parse() {
        if (yaml_node.IsNull() || !yaml_node.IsMap()) {
            RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "YAML file is not a map");
            return false;
        }

        const auto& chains_node = yaml_node["chains"];
        if (!chains_node || !chains_node.IsMap()) {
            RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "Missing or invalid 'chains' section in YAML file");
            return false;
        }

        for (const auto& chain_entry : chains_node) {
            // chain_entry is a std::pair<YAML::Node, YAML::Node>
            const std::string chain_name = chain_entry.first.as<std::string>();
            const auto& chain_node = chain_entry.second;

            // Skip empty chain names
            if (chain_name.empty()) {
                const auto msg = std::string("Chain with empty name in YAML file");
                RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "%s", msg.c_str());
                throw std::runtime_error(msg);
            }

            // Validate chain node structure
            if (!chain_node.IsMap()) {
                const auto msg = "Chain '" + chain_name + "' is not a map";
                RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "%s", msg.c_str());
                throw std::runtime_error(msg);
            }

            // Validate callbacks field
            const auto& callbacks_node = chain_node[CALLBACKS_KEY];
            if (!callbacks_node || !callbacks_node.IsSequence() || callbacks_node.size() == 0) {
                const auto msg = "Chain '" + chain_name + "' has missing or empty '" + CALLBACKS_KEY + "' field";
                RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "%s", msg.c_str());
                throw std::runtime_error(msg);
            }

            // Validate deadline field
            const auto& deadline_node = chain_node[DEADLINE_KEY];
            if (!deadline_node || !deadline_node.IsScalar()) {
                const auto msg = "Chain '" + chain_name + "' has missing or invalid '" + DEADLINE_KEY + "' field";
                RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "%s", msg.c_str());
                throw std::runtime_error(msg);
            }

            // Validate period field
            const auto& period_node = chain_node[PERIOD_KEY];
            if (!period_node || !period_node.IsScalar()) {
                const auto msg = "Chain '" + chain_name + "' has missing or invalid '" + PERIOD_KEY + "' field";
                RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "%s", msg.c_str());
                throw std::runtime_error(msg);
            }

            // Parse and store the chain
            try {
                const auto callbacks_list = callbacks_node.as<std::vector<std::string>>();
                const auto deadline = deadline_node.as<std::uint32_t>();
                const auto period = period_node.as<std::uint32_t>();

                user_chains[chain_name] = userChain{chain_name, callbacks_list, deadline, period};
                RCLCPP_DEBUG(rclcpp::get_logger("ChainYamlParser"), "Successfully parsed chain '%s' with %zu callbacks",
                             chain_name.c_str(), callbacks_list.size());
            } catch (const YAML::Exception& e) {
                const auto msg = "Failed to parse chain '" + chain_name + "': " + e.what();
                RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "%s", msg.c_str());
                throw std::runtime_error(msg);
            }
        }

        if (user_chains.empty()) {
            RCLCPP_ERROR(rclcpp::get_logger("ChainYamlParser"), "No valid chains found in YAML file");
            return false;
        }

        return true;
    }
} // namespace preemptive_executor