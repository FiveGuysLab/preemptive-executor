#include "preemptive_executor/callback_registry.hpp"

#include <boost/uuid/uuid_io.hpp>

namespace preemptive_executor {

CallbackRegistry::CallbackRegistry(const WeakCallbackGroupsToNodesMap& weak_groups_to_nodes,
                                   const std::unordered_map<std::string, userChain>& user_chains) {
  for (const auto& pair : weak_groups_to_nodes) {
    auto group = pair.first.lock();
    auto node = pair.second.lock();

    if (group == nullptr || node == nullptr) {
      continue;
    }

    if (!group->can_be_taken_from().load()) {
      continue;
    }

    // Collect all callback entities from callback groups
    group->collect_all_ptrs(
        [this, group, node](const rclcpp::SubscriptionBase::SharedPtr& subscription) {
          const auto callback_entity = CallbackEntity(subscription);
          callback_map_.emplace(get_callback_name(callback_entity), CallbackInfo{callback_entity, group, node});
        },
        [this, group, node](const rclcpp::ServiceBase::SharedPtr& service) {
          const auto callback_entity = CallbackEntity(service);
          callback_map_.emplace(get_callback_name(callback_entity), CallbackInfo{callback_entity, group, node});
        },
        [this, group, node](const rclcpp::ClientBase::SharedPtr& client) {
          const auto callback_entity = CallbackEntity(client);
          callback_map_.emplace(get_callback_name(callback_entity), CallbackInfo{callback_entity, group, node});
        },
        [this, group, node](const rclcpp::TimerBase::SharedPtr& timer) {
          const auto callback_entity = CallbackEntity(timer);
          callback_map_.emplace(get_callback_name(callback_entity), CallbackInfo{callback_entity, group, node});
        },
        [this, group, node](const rclcpp::Waitable::SharedPtr& waitable) {
          const auto callback_entity = CallbackEntity(waitable);
          callback_map_.emplace(get_callback_name(callback_entity), CallbackInfo{callback_entity, group, node});
        });
  }

  // Build adjacency list and in-degree map for user chains
  for (const auto& pair : user_chains) {
    const auto& callbacks = pair.second.callbacks;
    if (callbacks.size() < 2) {
      adjacency_list_[callbacks[0]] = CallbackAdjacencyInfo_{};
      continue;
    }
    for (size_t i = 1; i < callbacks.size(); i++) {
      adjacency_list_[callbacks[i - 1]].outgoing.emplace(callbacks[i]);
      adjacency_list_[callbacks[i]].indegree++;
      adjacency_list_[callbacks[i]].min_deadline =
          std::min(adjacency_list_[callbacks[i]].min_deadline, pair.second.deadline);
    }
    adjacency_list_[callbacks[0]].min_deadline = std::min(adjacency_list_[callbacks[0]].min_deadline, pair.second.deadline);
  }
}

const void* CallbackEntity::get_raw_pointer() const {
  return std::visit([](auto&& ptr) -> const void* { return ptr.get(); }, entity_);
}

std::string CallbackRegistry::generate_threadgroup_id() {
  boost::uuids::random_generator generator;
  boost::uuids::uuid uuid = generator();
  return boost::uuids::to_string(uuid);
}

// Break user chains into threads based on graph structure and in-degrees
void CallbackRegistry::callback_threadgroup_allocation() {
  std::map<uint32_t, std::vector<std::string>> deadline_to_threadgroup_id_map = {};
  for (const auto& pair : adjacency_list_) {
    if (pair.second.indegree == 0) {
      recursive_traversal(pair.first, "", deadline_to_threadgroup_id_map);
    }
  }
  // Assign fixed priority to threadgroups (deadline monotonic) starting from 1
  uint16_t fixed_priority_counter = 1;
  for (const auto& pair : deadline_to_threadgroup_id_map) {
    for (const auto& threadgroup_id : pair.second) {
      // std::map iteration is increasing order so deadline monotonic is
      // maintained Need to check if already assigned since mutex threadgroups
      // could have multiple entries in the map
      if (thread_callback_map_[threadgroup_id].fixed_priority == 0) {
        thread_callback_map_[threadgroup_id].fixed_priority = fixed_priority_counter++;
      }
    }
  }
}

void CallbackRegistry::recursive_traversal(
    const std::string& callback_name, std::string threadgroup_id,
    std::map<uint32_t, std::vector<std::string>>& deadline_to_threadgroup_id_map) {
  const auto callback_it = callback_map_.find(callback_name);
  if (callback_it == callback_map_.end()) {
    throw std::runtime_error("Callback not found in callback map");
  }
  auto& callback_info = callback_it->second;
  // Acts as a visited set to avoid unnecessary recursion
  if (!callback_info.threadgroup_id.empty()) {
    return;
  }

  bool is_mutex_group = callback_info.callback_group->type() == rclcpp::CallbackGroupType::MutuallyExclusive;
  // First callback in new threadgroup or mutually exclusive callback group
  if (threadgroup_id.empty() || is_mutex_group) {
    // For mutex case, use existing threadgroup_id if exists
    std::string new_threadgroup_id;
    if (is_mutex_group && mutex_threadgroup_map_.contains(callback_info.callback_group)) {
      new_threadgroup_id = mutex_threadgroup_map_[callback_info.callback_group];
    } else {
      new_threadgroup_id = generate_threadgroup_id();
      if (is_mutex_group) {
        mutex_threadgroup_map_[callback_info.callback_group] = new_threadgroup_id;
      }
    }
    thread_callback_map_[new_threadgroup_id].callbacks.push_back(callback_name);
    callback_info.threadgroup_id = new_threadgroup_id;
    deadline_to_threadgroup_id_map[adjacency_list_[callback_name].min_deadline].push_back(new_threadgroup_id);

    if (threadgroup_id.empty()) {
      threadgroup_id = new_threadgroup_id;
    }
  }

  const auto& outgoing = adjacency_list_[callback_name].outgoing;
  switch (outgoing.size()) {
    case 1: {
      const auto& next_callback = *outgoing.begin();
      // Next callback has a single incoming edge
      if (adjacency_list_[next_callback].indegree == 1) {
        thread_callback_map_[threadgroup_id].callbacks.push_back(next_callback);

        if (const auto next_it = callback_map_.find(next_callback); next_it != callback_map_.end()) {
          next_it->second.threadgroup_id = threadgroup_id;
        }
        recursive_traversal(next_callback, threadgroup_id, deadline_to_threadgroup_id_map);
        break;
      }
      [[fallthrough]];
    }
    default: {
      // Divergent case with N outgoing leads to N new threadgroups or
      // convergent case where next callback has multiple incoming edges
      for (const auto& next_callback : outgoing) {
        recursive_traversal(next_callback, "", deadline_to_threadgroup_id_map);
      }
      break;
    }
  }
}

std::string CallbackRegistry::get_callback_name(const CallbackEntity& entity) {
  /* todo */
  return "";
}

}  // namespace preemptive_executor