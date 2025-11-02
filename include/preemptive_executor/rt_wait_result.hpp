#ifndef PREEMPTIVE_EXECUTOR__WAIT_RESULT_HPP_
#define PREEMPTIVE_EXECUTOR__WAIT_RESULT_HPP_

#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include "rcl/wait.h"

#include "rclcpp/macros.hpp"
#include "rclcpp/wait_result_kind.hpp"

#include "rclcpp/client.hpp"
#include "rclcpp/service.hpp"
#include "rclcpp/subscription_base.hpp"
#include "rclcpp/timer.hpp"

namespace preemptive_executor
{

/**
 * NOTE:
 * 
 * This is the class where the bulk of our logic will go.
 * 
 * This class has next_ready_xxx functions, but we want to call something like
 *    wait_result_->take_all(our_map)
 * to flush everything into our map's readyQs
 * 
 * We will also need to take out all the messages that are available, but this is something
 * I (viraj) will do later. You can ignore that for now
 * 
 * For now, let that function that you write just work on subsciptions. I'll think
 * of something later for everything else.
 * 
 */



// TODO(wjwwood): the union-like design of this class could be replaced with
//   std::variant, when we have access to that...
/// Interface for introspecting a wait set after waiting on it.
/**
 * This class:
 *
 *   - provides the result of waiting, i.e. ready, timeout, or empty, and
 *   - holds the ownership of the entities of the wait set, if needed, and
 *   - provides the necessary information for iterating over the wait set.
 *
 * This class is only valid as long as the wait set which created it is valid,
 * and it must be deleted before the wait set is deleted, as it contains a
 * back reference to the wait set.
 *
 * An instance of this, which is returned from rclcpp::RTWaitSetTemplate::wait(),
 * will cause the wait set to keep ownership of the entities because it only
 * holds a reference to the sequences of them, rather than taking a copy.
 * Also, in the thread-safe case, an instance of this will cause the wait set,
 * to block calls which modify the sequences of the entities, e.g. add/remove
 * guard condition or subscription methods.
 *
 * \tparam WaitSetT The wait set type which created this class.
 */
template<class WaitSetT>
class RTWaitResult final
{
public:
  /// Create RTWaitResult from a "ready" result.
  /**
   * \param[in] wait_set A reference to the wait set, which this class
   *   will keep for the duration of its lifetime.
   * \return a RTWaitResult from a "ready" result.
   */
  static
  RTWaitResult
  from_ready_wait_result_kind(WaitSetT & wait_set)
  {
    return RTWaitResult(rclcpp::WaitResultKind::Ready, wait_set);
  }

  /// Create RTWaitResult from a "timeout" result.
  static
  RTWaitResult
  from_timeout_wait_result_kind()
  {
    return RTWaitResult(rclcpp::WaitResultKind::Timeout);
  }

  /// Create RTWaitResult from a "empty" result.
  static
  RTWaitResult
  from_empty_wait_result_kind()
  {
    return RTWaitResult(rclcpp::WaitResultKind::Empty);
  }

  /// Return the kind of the RTWaitResult.
  rclcpp::WaitResultKind
  kind() const
  {
    return wait_result_kind_;
  }

  /// Return the rcl wait set.
  /**
   * \return const rcl wait set.
   * \throws std::runtime_error if the class cannot access wait set when the result was not ready
   */
  const WaitSetT &
  get_wait_set() const
  {
    if (this->kind() != rclcpp::WaitResultKind::Ready) {
      throw std::runtime_error("cannot access wait set when the result was not ready");
    }
    // This should never happen, defensive (and debug mode) check only.
    assert(wait_set_pointer_);
    return *wait_set_pointer_;
  }

  /// Return the rcl wait set.
  /**
   * \return rcl wait set.
   * \throws std::runtime_error if the class cannot access wait set when the result was not ready
   */
  WaitSetT &
  get_wait_set()
  {
    if (this->kind() != rclcpp::WaitResultKind::Ready) {
      throw std::runtime_error("cannot access wait set when the result was not ready");
    }
    // This should never happen, defensive (and debug mode) check only.
    assert(wait_set_pointer_);
    return *wait_set_pointer_;
  }

  RTWaitResult(RTWaitResult && other) noexcept
  : wait_result_kind_(other.wait_result_kind_),
    wait_set_pointer_(std::exchange(other.wait_set_pointer_, nullptr))
  {}

  ~RTWaitResult()
  {
    if (wait_set_pointer_) {
      wait_set_pointer_->wait_result_release();
    }
  }

  /// Get the next ready timer and its index in the wait result, but do not clear it.
  /**
   * The returned timer is not cleared automatically, as it the case with the
   * other next_ready_*()-like functions.
   * Instead, this function returns the timer and the index that identifies it
   * in the wait result, so that it can be cleared (marked as taken or used)
   * in a separate step with clear_timer_with_index().
   * This is necessary in some multi-threaded executor implementations.
   *
   * If the timer is not cleared using the index, subsequent calls to this
   * function will return the same timer.
   *
   * If there is no ready timer, then nullptr will be returned and the index
   * will be invalid and should not be used.
   *
   * \param[in] start_index index at which to start searching for the next ready
   *   timer in the wait result. If the start_index is out of bounds for the
   *   list of timers in the wait result, then {nullptr, start_index} will be
   *   returned. Defaults to 0.
   * \return next ready timer pointer and its index in the wait result, or
   *   {nullptr, start_index} if none was found.
   */
  std::pair<std::shared_ptr<rclcpp::TimerBase>, size_t>
  peek_next_ready_timer(size_t start_index = 0)
  {
    check_wait_result_dirty();
    auto ret = std::shared_ptr<rclcpp::TimerBase>{nullptr};
    size_t ii = start_index;
    if (this->kind() == rclcpp::WaitResultKind::Ready) {
      auto & wait_set = this->get_wait_set();
      auto & rcl_wait_set = wait_set.storage_get_rcl_wait_set();
      for (; ii < wait_set.size_of_timers(); ++ii) {
        if (rcl_wait_set.timers[ii] != nullptr) {
          ret = wait_set.timers(ii);
          if (ret) {
            break;
          }
        }
      }
    }
    return {ret, ii};
  }

  /// Clear the timer at the given index.
  /**
   * Clearing a timer from the wait result prevents it from being returned by
   * the peek_next_ready_timer() on subsequent calls.
   *
   * The index should come from the peek_next_ready_timer() function, and
   * should only be used with this function if the timer pointer was valid.
   *
   * \throws std::out_of_range if the given index is out of range
   */
  void
  clear_timer_with_index(size_t index)
  {
    auto & wait_set = this->get_wait_set();
    auto & rcl_wait_set = wait_set.storage_get_rcl_wait_set();
    if (index >= wait_set.size_of_timers()) {
      throw std::out_of_range("given timer index is out of range");
    }
    rcl_wait_set.timers[index] = nullptr;
  }

  /// Get the next ready subscription, clearing it from the wait result.
  std::shared_ptr<rclcpp::SubscriptionBase>
  next_ready_subscription()
  {
    check_wait_result_dirty();
    auto ret = std::shared_ptr<rclcpp::SubscriptionBase>{nullptr};
    if (this->kind() == rclcpp::WaitResultKind::Ready) {
      auto & wait_set = this->get_wait_set();
      auto & rcl_wait_set = wait_set.storage_get_rcl_wait_set();
      for (size_t ii = 0; ii < wait_set.size_of_subscriptions(); ++ii) {
        if (rcl_wait_set.subscriptions[ii] != nullptr) {
          ret = wait_set.subscriptions(ii);
          rcl_wait_set.subscriptions[ii] = nullptr;
          if (ret) {
            break;
          }
        }
      }
    }
    return ret;
  }

  /// Get the next ready service, clearing it from the wait result.
  std::shared_ptr<rclcpp::ServiceBase>
  next_ready_service()
  {
    check_wait_result_dirty();
    auto ret = std::shared_ptr<rclcpp::ServiceBase>{nullptr};
    if (this->kind() == rclcpp::WaitResultKind::Ready) {
      auto & wait_set = this->get_wait_set();
      auto & rcl_wait_set = wait_set.storage_get_rcl_wait_set();
      for (size_t ii = 0; ii < wait_set.size_of_services(); ++ii) {
        if (rcl_wait_set.services[ii] != nullptr) {
          ret = wait_set.services(ii);
          rcl_wait_set.services[ii] = nullptr;
          if (ret) {
            break;
          }
        }
      }
    }
    return ret;
  }

  /// Get the next ready client, clearing it from the wait result.
  std::shared_ptr<rclcpp::ClientBase>
  next_ready_client()
  {
    check_wait_result_dirty();
    auto ret = std::shared_ptr<rclcpp::ClientBase>{nullptr};
    if (this->kind() == rclcpp::WaitResultKind::Ready) {
      auto & wait_set = this->get_wait_set();
      auto & rcl_wait_set = wait_set.storage_get_rcl_wait_set();
      for (size_t ii = 0; ii < wait_set.size_of_clients(); ++ii) {
        if (rcl_wait_set.clients[ii] != nullptr) {
          ret = wait_set.clients(ii);
          rcl_wait_set.clients[ii] = nullptr;
          if (ret) {
            break;
          }
        }
      }
    }
    return ret;
  }

  /// Get the next ready waitable, clearing it from the wait result.
  std::shared_ptr<rclcpp::Waitable>
  next_ready_waitable()
  {
    check_wait_result_dirty();
    auto waitable = std::shared_ptr<rclcpp::Waitable>{nullptr};
    auto data = std::shared_ptr<void>{nullptr};

    if (this->kind() == rclcpp::WaitResultKind::Ready) {
      auto & wait_set = this->get_wait_set();
      auto & rcl_wait_set = wait_set.get_rcl_wait_set();
      while (next_waitable_index_ < wait_set.size_of_waitables()) {
        auto cur_waitable = wait_set.waitables(next_waitable_index_++);
        if (cur_waitable != nullptr && cur_waitable->is_ready(rcl_wait_set)) {
          waitable = cur_waitable;
          break;
        }
      }
    }

    return waitable;
  }

private:
  RCLCPP_DISABLE_COPY(RTWaitResult)

  explicit RTWaitResult(rclcpp::WaitResultKind wait_result_kind)
  : wait_result_kind_(wait_result_kind)
  {
    // Should be enforced by the static factory methods on this class.
    assert(rclcpp::WaitResultKind::Ready != wait_result_kind);
  }

  RTWaitResult(rclcpp::WaitResultKind wait_result_kind, WaitSetT & wait_set)
  : wait_result_kind_(wait_result_kind),
    wait_set_pointer_(&wait_set)
  {
    // Should be enforced by the static factory methods on this class.
    assert(rclcpp::WaitResultKind::Ready == wait_result_kind);
    // Secure thread-safety (if provided) and shared ownership (if needed).
    this->get_wait_set().wait_result_acquire();
  }

  /// Check if the wait result is invalid because the wait set was modified.
  void
  check_wait_result_dirty()
  {
    // In the case that the wait set was modified while the result was out,
    // we must mark the wait result as no longer valid
    if (wait_set_pointer_ && this->get_wait_set().wait_result_dirty_) {
      this->wait_result_kind_ = rclcpp::WaitResultKind::Invalid;
    }
  }

  rclcpp::WaitResultKind wait_result_kind_;

  WaitSetT * wait_set_pointer_ = nullptr;

  size_t next_waitable_index_ = 0;
};

}  // namespace preemptive_executor

#endif  // PREEMPTIVE_EXECUTOR__WAIT_RESULT_HPP_
