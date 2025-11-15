// Copyright 2015 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PREEMPTIVE_EXECUTOR__MEMORY_STRATEGY_HPP_
#define PREEMPTIVE_EXECUTOR__MEMORY_STRATEGY_HPP_

#include <list>
#include <map>
#include <memory>
#include <vector>

#include "rcl/allocator.h"
#include "rcl/wait.h"

#include "rclcpp/any_executable.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/node_interfaces/node_base_interface.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/timer.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rclcpp/waitable.hpp"
#include "rclcpp/memory_strategy.hpp"

namespace preemptive_executor
{
namespace memory_strategy
{

/// Delegate for handling memory allocations while the Executor is executing.
/**
 * By default, the memory strategy dynamically allocates memory for structures that come in from
 * the rmw implementation after the executor waits for work, based on the number of entities that
 * come through.
 */
class RCLCPP_PUBLIC RTMemoryStrategy : public rclcpp::memory_strategy::MemoryStrategy
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(RTMemoryStrategy)
  using WeakCallbackGroupsToNodesMap = std::map<rclcpp::CallbackGroup::WeakPtr,
      rclcpp::node_interfaces::NodeBaseInterface::WeakPtr,
      std::owner_less<rclcpp::CallbackGroup::WeakPtr>>;

  virtual ~RTMemoryStrategy() = default;

  virtual void take_ready_subscriptions(
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes,
    std::vector<rclcpp::SubscriptionBase::SharedPtr> & ready_subscriptions) = 0;

  virtual void take_ready_timers(
    const WeakCallbackGroupsToNodesMap & weak_groups_to_nodes,
    std::vector<rclcpp::TimerBase::SharedPtr> & ready_timers) = 0;
};

}  // namespace memory_strategy
}  // namespace preemptive_executor

#endif  // PREEMPTIVE_EXECUTOR__MEMORY_STRATEGY_HPP_
