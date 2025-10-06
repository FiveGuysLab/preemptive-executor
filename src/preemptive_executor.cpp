#include "preemptive_executor/preemptive_executor.hpp"

#include <rcl/wait.h>
#include <rclcpp/memory_strategies.hpp>
#include <rclcpp/executors/executor.hpp>

#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;

namespace preemptive_executor
{

    void PreemptiveExecutor::wait_for_work(std::chrono::nanoseconds timeout)
    {
        auto memory_strategy = memory_strategy_;
        if (!memory_strategy)
        {
            memory_strategy = rclcpp::memory_strategies::create_default_strategy();
        }

        rcl_wait_set_t wait_set = rcl_get_zero_initialized_wait_set();
        rcl_allocator_t allocator = memory_strategy->get_allocator();

        // Guard conditions: 0, Subscriptions: 2, Services: 0, Clients: 2, Events: 0, Timers: 1 (defaults)
        rcl_ret_t ret = rcl_wait_set_init(
            &wait_set,
            memory_strategy->number_of_ready_subscriptions(),
            memory_strategy->number_of_guard_conditions(),
            memory_strategy->number_of_ready_timers(),
            memory_strategy->number_of_ready_clients(),
            memory_strategy->number_of_ready_services(),
            memory_strategy->number_of_ready_events(),
            this->context_->get_rcl_context().get(),
            allocator);
        if (ret != RCL_RET_OK)
        {
            throw std::runtime_error("Couldn't initialize wait set");
        }

        // cleanup function for the wait set
        auto finalize_waitset = [&wait_set]()
        {
            if (rcl_wait_set_fini(&wait_set) != RCL_RET_OK)
            {
                RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "Couldn't finalize wait set");
            }
        };

        try
        {
            if (!memory_strategy->add_handles_to_wait_set(&wait_set))
            {
                finalize_waitset(); //cleanup the wait set
                throw std::runtime_error("Couldn't fill wait set");
            }

            int64_t timeout_ns = timeout.count();
            if (timeout_ns < 0)
            {
                timeout_ns = 0;
            }

            ret = rcl_wait(&wait_set, timeout_ns);
            if (ret == RCL_RET_TIMEOUT)
            {
                finalize_waitset(); //cleanup the wait set
                return;
            }
            else if (ret != RCL_RET_OK)
            {
                finalize_waitset(); //cleanup the wait set
                throw std::runtime_error("rcl_wait() failed");
            }

            memory_strategy->remove_null_handles(&wait_set);

            // After waiting, get the next ready executable using default order
            rclcpp::AnyExecutable any_exec;
            memory_strategy->get_next_subscription(any_exec, weak_nodes_);
            if (!any_exec.subscription)
            {
                memory_strategy->get_next_service(any_exec, weak_nodes_);
            }
            if (!any_exec.service)
            {
                memory_strategy->get_next_client(any_exec, weak_nodes_);
            }
            if (!any_exec.client)
            {
                memory_strategy->get_next_timer(any_exec, weak_nodes_);
            }
            if (!any_exec.timer)
            {
                memory_strategy->get_next_waitable(any_exec, weak_nodes_);
            }

            // If we found something ready, map it to a ReadyQueue via callback->thread_group->worker_group
            if (any_exec.subscription || any_exec.service || any_exec.client || any_exec.timer || any_exec.waitable)
            {
                int callback_id = 0;
                if (any_exec.subscription)
                {
                    callback_id = static_cast<int>(reinterpret_cast<intptr_t>(any_exec.subscription.get()));
                }
                else if (any_exec.service)
                {
                    callback_id = static_cast<int>(reinterpret_cast<intptr_t>(any_exec.service.get()));
                }
                else if (any_exec.client)
                {
                    callback_id = static_cast<int>(reinterpret_cast<intptr_t>(any_exec.client.get()));
                }
                else if (any_exec.timer)
                {
                    callback_id = static_cast<int>(reinterpret_cast<intptr_t>(any_exec.timer.get()));
                }
                else if (any_exec.waitable)
                {
                    callback_id = static_cast<int>(reinterpret_cast<intptr_t>(any_exec.waitable.get()));
                }

                // Find the thread group for this callback id
                auto tg_it = callback_id_thead_group_map.find(callback_id);
                if (tg_it != callback_id_thead_group_map.end() && tg_it->second != nullptr)
                {
                    auto thread_group_ptr = tg_it->second; // *ThreadGroup

                    // Find the worker groups associated with this thread group
                    auto wg_it = thread_group_worker_map.find(thread_group_ptr);
                    if (wg_it != thread_group_worker_map.end() && !wg_it->second.empty())
                    {
                        auto worker_group_ptr = wg_it->second.front(); // *WorkerGroup
                        if (worker_group_ptr != nullptr)
                        {
                            // Enqueue into the worker group's ready queue
                            {
                                std::lock_guard<std::mutex> lk(worker_group_ptr->ready_queue.mutex);
                                worker_group_ptr->ready_queue.queue.push(any_exec);
                            }
                        }
                        // Wake one worker in this group if a semaphore exists
                        if (worker_group_ptr->semaphore)
                        {
                            worker_group_ptr->semaphore->release();
                        }
                    }
                }
            }

            finalize_waitset();
        }
        catch (...)
        {
            finalize_waitset();
            throw;
        }
    }

} // namespace preemptive_executor
