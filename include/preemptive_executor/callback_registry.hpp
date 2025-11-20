#ifndef CALLBACK_REGISTRY_HPP
#define CALLBACK_REGISTRY_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "preemptive_executor/yaml_parser.hpp"
#include "rclcpp/callback_group.hpp"
#include "rclcpp/client.hpp"
#include "rclcpp/memory_strategy.hpp"
#include "rclcpp/node_interfaces/node_base_interface.hpp"
#include "rclcpp/service.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/timer.hpp"
#include "rclcpp/waitable.hpp"
#include "preemptive_executor/preemptive_executor.hpp"

namespace preemptive_executor {

enum class CallbackEntityType { Subscription, Timer, Service, Client, Waitable };

// Type aliases for convenience
using SubscriptionPtr = rclcpp::SubscriptionBase::SharedPtr;
using TimerPtr = rclcpp::TimerBase::SharedPtr;
using ServicePtr = rclcpp::ServiceBase::SharedPtr;
using ClientPtr = rclcpp::ClientBase::SharedPtr;
using WaitablePtr = rclcpp::Waitable::SharedPtr;

using EntityVariant = std::variant<SubscriptionPtr, TimerPtr, ServicePtr, ClientPtr, WaitablePtr>;

/**
 * Generic wrapper for all callback entity types, because i'm fucking sick of
 * this shit Uses std::variant
 */
class CallbackEntity {
 public:
  CallbackEntity(const SubscriptionPtr& sub) : entity_(sub), type_(CallbackEntityType::Subscription) {}
  CallbackEntity(const TimerPtr& timer) : entity_(timer), type_(CallbackEntityType::Timer) {}
  CallbackEntity(const ServicePtr& service) : entity_(service), type_(CallbackEntityType::Service) {}
  CallbackEntity(const ClientPtr& client) : entity_(client), type_(CallbackEntityType::Client) {}
  CallbackEntity(const WaitablePtr& waitable) : entity_(waitable), type_(CallbackEntityType::Waitable) {}

  CallbackEntityType get_type() const { return type_; }

  // Type-safe getters, it returns nullptr for wrong type
  SubscriptionPtr as_subscription() const {
    return std::holds_alternative<SubscriptionPtr>(entity_) ? std::get<SubscriptionPtr>(entity_) : nullptr;
  }

  TimerPtr as_timer() const {
    return std::holds_alternative<TimerPtr>(entity_) ? std::get<TimerPtr>(entity_) : nullptr;
  }

  ServicePtr as_service() const {
    return std::holds_alternative<ServicePtr>(entity_) ? std::get<ServicePtr>(entity_) : nullptr;
  }

  ClientPtr as_client() const {
    return std::holds_alternative<ClientPtr>(entity_) ? std::get<ClientPtr>(entity_) : nullptr;
  }

  WaitablePtr as_waitable() const {
    return std::holds_alternative<WaitablePtr>(entity_) ? std::get<WaitablePtr>(entity_) : nullptr;
  }

  const void* get_raw_pointer() const;
  const EntityVariant& get_generic_pointer() const { return entity_; }

  // Equality operator for use as map key
  bool operator==(const CallbackEntity& other) const { return get_raw_pointer() == other.get_raw_pointer(); }

 private:
  EntityVariant entity_;
  CallbackEntityType type_;
};

// Hash function for CallbackEntity to use as map key
struct CallbackEntityHash {
  std::size_t operator()(const CallbackEntity& entity) const {
    return std::hash<const void*>{}(entity.get_raw_pointer());
  }
};

struct CallbackInfo {
  CallbackEntity entity;
  rclcpp::CallbackGroup::SharedPtr callback_group;
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node;
  std::string callback_name;
  int threadgroup_id = 0;

  CallbackInfo(CallbackEntity e, rclcpp::CallbackGroup::SharedPtr group,
               rclcpp::node_interfaces::NodeBaseInterface::SharedPtr n, std::string name = "")
      : entity(std::move(e)), callback_group(group), node(n), callback_name(name) {}
};

struct ThreadGroupInfo {
  int threadgroup_id = 0;  // technically redundant since it's the map key, but kept for clarity
  std::vector<std::string> callbacks;
  uint16_t fixed_priority = 0;
  uint16_t num_threads = 1;
  bool is_mutex_group = false;
};

struct TimingExport {
  std::unique_ptr<std::unordered_map<void*, int>> callback_handle_to_threadgroup_id;
  std::unique_ptr<std::unordered_map<int, ThreadGroupAttributes>> threadgroup_attributes;
};

class CallbackRegistry {
  // Singleton
 public:
  using WeakCallbackGroupsToNodesMap = rclcpp::memory_strategy::MemoryStrategy::WeakCallbackGroupsToNodesMap;

  ~CallbackRegistry();

  static CallbackRegistry& get_instance(const WeakCallbackGroupsToNodesMap& weak_groups_to_nodes,
                                        const std::unordered_map<std::string, userChain>& user_chains) {
    static CallbackRegistry instance(weak_groups_to_nodes, user_chains);
    return instance;
  }

  // Can be called before or after CallbackRegistry construction
  static void register_callback_name(const CallbackEntity& entity, const std::string& name);

  // Get callback name from entity (assumes callback was registered)
  std::string get_callback_name(const CallbackEntity& entity) const;

  TimingExport callback_threadgroup_allocation();

 private:
  CallbackRegistry(const WeakCallbackGroupsToNodesMap& weak_groups_to_nodes,
                   const std::unordered_map<std::string, userChain>& user_chains);
  CallbackRegistry(const CallbackRegistry&) = delete;
  CallbackRegistry& operator=(const CallbackRegistry&) = delete;
  
  struct CallbackAdjacencyInfo_ {
    std::unordered_set<std::string> outgoing = {};
    std::uint8_t indegree = 0;
    std::vector<std::uint32_t> deadlines = {};
    std::vector<std::uint32_t> periods = {};
    std::uint32_t min_deadline = UINT32_MAX;
  };

  struct ThreadGroupAdjacencyInfo_ {
    std::unordered_set<int> outgoing = {};
    std::unordered_set<int> incoming = {};

    std::uint8_t indegree() const { return incoming.size(); }
  };

  std::unordered_map<std::string, CallbackAdjacencyInfo_> adjacency_list_;
  std::unordered_map<int, ThreadGroupAdjacencyInfo_> threadgroup_adjacency_list_;
  std::unordered_map<rclcpp::CallbackGroup::SharedPtr, int> mutex_threadgroup_map_;
  std::unordered_map<std::string, CallbackInfo> callback_map_;
  std::unordered_map<int, ThreadGroupInfo> threadgroup_callback_map_;

  int next_threadgroup_id_ = 1;  // Instance member since class is singleton
  static std::unordered_map<CallbackEntity, std::string, CallbackEntityHash>
      pre_registered_names_;  // Static map for pre-construction registration

  void recursive_callback_traversal(const std::string& callback_name, int threadgroup_id, int prev_threadgroup_id,
                                    std::map<uint32_t, std::vector<int>>& deadline_to_threadgroup_id_map);
  void recursive_threadgroup_traversal(int threadgroup_id);
  int generate_threadgroup_id();

  // Helper to register a callback entity (used by collect_all_ptrs lambdas)
  void register_callback_entity(const CallbackEntity& entity, rclcpp::CallbackGroup::SharedPtr group,
                                rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node);
  
  // Helper to export timing information
  TimingExport export_timing_information();
};
}  // namespace preemptive_executor

#endif