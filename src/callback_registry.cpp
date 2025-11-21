#include "preemptive_executor/callback_registry.hpp"

#include <cmath>
#include <algorithm>

namespace preemptive_executor {

// Initialize static pre-registration map
std::unordered_map<CallbackEntity, std::string, CallbackEntityHash> CallbackRegistry::pre_registered_names_{};

CallbackRegistry::CallbackRegistry(const WeakCallbackGroupsToNodesMap& weak_groups_to_nodes,
                                   const std::unordered_map<std::string, userChain>& user_chains) {

  std::cout << "Constructing CallbackRegistry" << std::endl;
  std::cout << "Weak groups to nodes size: " << weak_groups_to_nodes.size() << std::endl;
  for (const auto& pair : weak_groups_to_nodes) {
    auto group = pair.first.lock();
    auto node = pair.second.lock();

    if (group == nullptr || node == nullptr || !group->can_be_taken_from().load()) {
      continue;
    }

    // Collect all callback entities from callback groups
    // Assumes all callbacks have been pre-registered with names
    group->collect_all_ptrs(
        [this, group, node](const rclcpp::SubscriptionBase::SharedPtr& subscription) {
          register_callback_entity(CallbackEntity(subscription), group, node);
        },
        [this, group, node](const rclcpp::ServiceBase::SharedPtr& service) {
          register_callback_entity(CallbackEntity(service), group, node);
        },
        [this, group, node](const rclcpp::ClientBase::SharedPtr& client) {
          register_callback_entity(CallbackEntity(client), group, node);
        },
        [this, group, node](const rclcpp::TimerBase::SharedPtr& timer) {
          register_callback_entity(CallbackEntity(timer), group, node);
        },
        [this, group, node](const rclcpp::Waitable::SharedPtr& waitable) {
          register_callback_entity(CallbackEntity(waitable), group, node);
        });
  }

  // Build adjacency list and in-degree map for user chains
  for (const auto& pair : user_chains) {
    const auto& callbacks = pair.second.callbacks;
    if (callbacks.size() < 2) {
      adjacency_list_[callbacks[0]] = CallbackAdjacencyInfo_{};
      continue;
    }
    for (size_t i = 0; i < callbacks.size(); i++) {
      if (i > 0) {
        adjacency_list_[callbacks[i - 1]].outgoing.emplace(callbacks[i]);
        adjacency_list_[callbacks[i]].indegree++;
      }
      adjacency_list_[callbacks[i]].deadlines.push_back(pair.second.deadline);
      adjacency_list_[callbacks[i]].periods.push_back(pair.second.period);
      adjacency_list_[callbacks[i]].min_deadline =
          std::min(adjacency_list_[callbacks[i]].min_deadline, pair.second.deadline);
    }
  }
}

const void* CallbackEntity::get_raw_pointer() const {
  return std::visit([](auto&& ptr) -> const void* { return ptr.get(); }, entity_);
}

int CallbackRegistry::generate_threadgroup_id() { return next_threadgroup_id_++; }

// Break user chains into threads based on graph structure and in-degrees
TimingExport CallbackRegistry::callback_threadgroup_allocation() {
  std::map<uint32_t, std::vector<int>> deadline_to_threadgroup_id_map = {};
  for (const auto& pair : adjacency_list_) {
    if (pair.second.indegree == 0) {
      recursive_callback_traversal(pair.first, 0, 0, deadline_to_threadgroup_id_map);
    }
  }
  // Assign fixed priority to threadgroups (deadline monotonic) starting from 1
  uint16_t fixed_priority_counter = 1;
  for (const auto& pair : deadline_to_threadgroup_id_map) {
    for (const auto& threadgroup_id : pair.second) {
      // std::map iteration is increasing order so deadline monotonic is
      // maintained Need to check if already assigned since mutex threadgroups
      // could have multiple entries in the map
      auto& threadgroup_info = threadgroup_callback_map_[threadgroup_id];
      if (threadgroup_info.fixed_priority == 0) {
        threadgroup_info.fixed_priority = fixed_priority_counter++;
      }
    }
  }

  // Calculate number of threads for each threadgroup
  for (const auto& pair : threadgroup_adjacency_list_) {
    if (pair.second.indegree() == 0 && !threadgroup_callback_map_[pair.first].is_mutex_group) {
      recursive_threadgroup_traversal(pair.first);
    }
  }

  return export_timing_information();
}

TimingExport CallbackRegistry::export_timing_information() {
  TimingExport timing_export;
  timing_export.callback_handle_to_threadgroup_id = std::make_unique<std::unordered_map<void*, int>>();
  timing_export.threadgroup_attributes = std::make_unique<std::unordered_map<int, ThreadGroupAttributes>>();

  for (const auto& pair : callback_map_) {
    const auto& callback_info = pair.second;
    void* raw_ptr = const_cast<void*>(callback_info.entity.get_raw_pointer());
    if (callback_info.threadgroup_id == 0) {
      throw std::runtime_error("Callback " + callback_info.callback_name + " has not been assigned to any threadgroup");
    }
    (*timing_export.callback_handle_to_threadgroup_id)[raw_ptr] = callback_info.threadgroup_id;
  }

  for (const auto& pair : threadgroup_callback_map_) {
    const auto& threadgroup_info = pair.second;
    ThreadGroupAttributes attributes(threadgroup_info.threadgroup_id, threadgroup_info.num_threads,
                                    threadgroup_info.fixed_priority, threadgroup_info.is_mutex_group);
    (*timing_export.threadgroup_attributes).emplace(threadgroup_info.threadgroup_id, attributes);
  }

  // clear all internal data structures
  adjacency_list_.clear();
  threadgroup_adjacency_list_.clear();
  mutex_threadgroup_map_.clear();
  callback_map_.clear();
  threadgroup_callback_map_.clear();
  next_threadgroup_id_ = 1;

  return timing_export;
}

void CallbackRegistry::recursive_callback_traversal(const std::string& callback_name, int threadgroup_id, int prev_threadgroup_id,
                                           std::map<uint32_t, std::vector<int>>& deadline_to_threadgroup_id_map) {
  const auto callback_it = callback_map_.find(callback_name);
  if (callback_it == callback_map_.end()) {
    throw std::runtime_error("Callback not found in callback map");
  }
  auto& callback_info = callback_it->second;
  // Acts as a visited set to avoid unnecessary recursion
  if (callback_info.threadgroup_id != 0) {
    return;
  }

  bool is_mutex_group = callback_info.callback_group->type() == rclcpp::CallbackGroupType::MutuallyExclusive;
  // First callback in new threadgroup or mutually exclusive callback group
  if (threadgroup_id == 0 || is_mutex_group) {
    // For mutex case, use existing threadgroup_id if exists
    int new_threadgroup_id;
    if (is_mutex_group && mutex_threadgroup_map_.contains(callback_info.callback_group)) {
      new_threadgroup_id = mutex_threadgroup_map_[callback_info.callback_group];
    } else {
      new_threadgroup_id = generate_threadgroup_id();
      if (is_mutex_group) {
        mutex_threadgroup_map_[callback_info.callback_group] = new_threadgroup_id;
        threadgroup_callback_map_[new_threadgroup_id].is_mutex_group = true;
      }
    }
    threadgroup_callback_map_[new_threadgroup_id].callbacks.push_back(callback_name);
    threadgroup_callback_map_[new_threadgroup_id].threadgroup_id = new_threadgroup_id;
    callback_info.threadgroup_id = new_threadgroup_id;
    deadline_to_threadgroup_id_map[adjacency_list_[callback_name].min_deadline].push_back(new_threadgroup_id);

    if (threadgroup_id == 0) {
      threadgroup_id = new_threadgroup_id;
      if (prev_threadgroup_id != 0) {
        threadgroup_adjacency_list_[prev_threadgroup_id].outgoing.insert(new_threadgroup_id);
        threadgroup_adjacency_list_[new_threadgroup_id].incoming.insert(prev_threadgroup_id);
      }
    }
  }

  const auto& outgoing = adjacency_list_[callback_name].outgoing;
  const auto& next_callback = *outgoing.begin();
  if (adjacency_list_[next_callback].indegree == 1 && outgoing.size() == 1) {
    // Next callback has a single incoming edge
    threadgroup_callback_map_[threadgroup_id].callbacks.push_back(next_callback);

    // if (!callback_map_.contains(next_callback)) {
    //   throw std::runtime_error("Callback not found in callback map");
    // }
    // callback_map_[next_callback].threadgroup_id = threadgroup_id;
    if (const auto next_it = callback_map_.find(next_callback); next_it != callback_map_.end()) {
      next_it->second.threadgroup_id = threadgroup_id;
    } else {
      throw std::runtime_error("Callback not found in callback map");
    }
    return recursive_callback_traversal(next_callback, threadgroup_id, prev_threadgroup_id, deadline_to_threadgroup_id_map);
  }

  // Divergent case with N outgoing leads to N new threadgroups or
  // convergent case where next callback has multiple incoming edges
  for (const auto& next_callback : outgoing) {
    if (!threadgroup_callback_map_[threadgroup_id].is_mutex_group) {
      prev_threadgroup_id = threadgroup_id;
    }
    recursive_callback_traversal(next_callback, 0, threadgroup_id, deadline_to_threadgroup_id_map);
  }
  return;
}

void CallbackRegistry::recursive_threadgroup_traversal(int threadgroup_id) {
  auto& threadgroup_info = threadgroup_callback_map_[threadgroup_id];
  threadgroup_info.num_threads = 0;
  if (threadgroup_adjacency_list_[threadgroup_id].indegree() == 0) {
    // Assuming roots have the same period if part of multiple chains
    const auto& first_callback_info = adjacency_list_[threadgroup_info.callbacks[0]];
    auto period = first_callback_info.periods[0];
    auto deadline = std::max_element(first_callback_info.deadlines.begin(), first_callback_info.deadlines.end());
    threadgroup_info.num_threads = std::ceil(*deadline / period);
  } else {
    for (auto& prev_threadgroup_id : threadgroup_adjacency_list_[threadgroup_id].incoming) {
      threadgroup_info.num_threads += threadgroup_callback_map_[prev_threadgroup_id].num_threads;
    }
  }
  for (auto& next_threadgroup_id : threadgroup_adjacency_list_[threadgroup_id].outgoing) {
    recursive_threadgroup_traversal(next_threadgroup_id);
  }
}

void CallbackRegistry::register_callback_entity(const CallbackEntity& entity, rclcpp::CallbackGroup::SharedPtr group,
                                                rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node) {
  const auto entity_it = pre_registered_names_.find(entity);
  if (entity_it == pre_registered_names_.end()) {
    std::cout << "Callback entity not pre-registered" << " Type=" << static_cast<int>(entity.get_type()) << std::endl;
    return;
  }
  std::cout << "Registering callback entity with name: " << entity_it->second << std::endl;

  const std::string callback_name = entity_it->second;
  callback_map_.emplace(callback_name, CallbackInfo{entity, group, node, callback_name});
}

// Static method - can be called before or after construction
void CallbackRegistry::register_callback_name(const CallbackEntity& entity, const std::string& name) {
  if (name.empty()) {
    throw std::runtime_error("Callback name is empty");
  }

  if (pre_registered_names_.contains(entity)) {
    throw std::runtime_error("Callback already registered");
  }
  pre_registered_names_[entity] = name;
}

std::string CallbackRegistry::get_callback_name(const CallbackEntity& entity) const {
  // Check instance map (contains all pre-registered names copied during construction)
  const auto entity_it = pre_registered_names_.find(entity);
  if (entity_it != pre_registered_names_.end()) {
    return entity_it->second;
  }

  // are there implicit callbacks that users don't explicitly define? hope not...
  // If not found, check if it's already in callback_map_ (was scanned)
  // for (const auto& pair : callback_map_) {
  //   if (pair.second.entity == entity) {
  //     return pair.second.callback_name;
  //   }
  // }

  // Should not happen if callback was registered
  throw std::runtime_error("Callback not registered");
}

}  // namespace preemptive_executor