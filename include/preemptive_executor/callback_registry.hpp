#ifndef CALLBACK_REGISTRY_HPP
#define CALLBACK_REGISTRY_HPP

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

 private:
  EntityVariant entity_;
  CallbackEntityType type_;
};

struct CallbackInfo {
  CallbackEntity entity;
  rclcpp::CallbackGroup::SharedPtr callback_group;
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node;
  std::string callback_name = "";
  std::string threadgroup_id = "";

  CallbackInfo(CallbackEntity e, rclcpp::CallbackGroup::SharedPtr group,
               rclcpp::node_interfaces::NodeBaseInterface::SharedPtr n)
      : entity(std::move(e)), callback_group(group), node(n) {}
};

class CallbackRegistry {
 public:
  using WeakCallbackGroupsToNodesMap = rclcpp::memory_strategy::MemoryStrategy::WeakCallbackGroupsToNodesMap;

  CallbackRegistry(const WeakCallbackGroupsToNodesMap& weak_groups_to_nodes,
                   const std::unordered_map<std::string, userChain>& user_chains);
  ~CallbackRegistry();

  // Single function that handles all callback entity types using std::visit
  std::string get_callback_name(const CallbackEntity& entity);

  void callback_threadgroup_allocation();

 private:
  struct CallbackAdjacencyInfo_ {
    std::vector<std::string> outgoing = {};
    std::uint8_t indegree = 0;
    std::uint32_t min_deadline = UINT32_MAX;
  };

  struct ThreadInfo_ {
    std::string thread_id;
    std::vector<std::string> callbacks;
    std::uint32_t min_deadline = UINT32_MAX;
    uint16_t fixed_priority = 0;
  };

  std::unordered_map<std::string, CallbackAdjacencyInfo_> adjacency_list_;
  std::unordered_map<rclcpp::CallbackGroup::SharedPtr, std::string> mutex_threadgroup_map_;
  std::unordered_map<std::string, CallbackInfo> callback_map_;
  std::unordered_map<std::string, ThreadInfo_> thread_callback_map_;

  void recursive_traversal(const std::string& callback_name, std::string threadgroup_id,
                           std::map<uint32_t, std::vector<std::string>>& deadline_to_threadgroup_id_map);
  std::string generate_threadgroup_id();
};
}  // namespace preemptive_executor

#endif