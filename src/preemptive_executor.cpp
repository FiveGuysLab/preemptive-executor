#include <chrono>
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include "preemptive_executor/preemptive_executor.hpp"
#include <linux/sched.h>

namespace preemptive_executor
{
    void set_fifo_prio(int priority, std::thread &t)
    {
        const auto param = sched_param{priority};
        pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
    }

    void worker_main(std::shared_ptr<WorkerGroup> worker_group)
    {

        // 1: set timing policy // NOTE: Handled by dispatcher
        // 2: register with thread group // NOTE: Registration handled by dispatcher

        while (true)
        {
            // 3: wait on worker group semaphore
            worker_group->semaphore->acquire();

            if (!rclcpp::ok())
            { // TODO: We didn't pass in the context, so this does nothing
                break;
            }

            // 4: acquire ready queue mutex and 5: pop from ready queue
            std::unique_ptr<BundledExecutable> exec = nullptr;
            {
                auto &rq = worker_group->ready_queue;
                std::lock_guard<std::mutex> guard(rq.mutex);

                if (rq.queue.empty() || rq.queue.front() == nullptr)
                {
                    throw std::runtime_error("Ready Q state invalid");
                }

                std::swap(exec, rq.queue.front());
                rq.queue.pop();
            }

            // TODO: check exec spinning with a lambda

            // 7: execute executable (placeholder; actual execution integrates with executor run loop)
            exec->run();
        }
    }

    PreemptiveExecutor::PreemptiveExecutor(const rclcpp::ExecutorOptions &options, memory_strategy::RTMemoryStrategy::SharedPtr rt_memory_strategy)
        : Executor(options), rt_memory_strategy_(rt_memory_strategy)
    {
        if (memory_strategy_ != rt_memory_strategy_)
        {
            RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "rt_memory_strategy must be a derivation of options.memory_strategy");
        }
    }

    void PreemptiveExecutor::spawn_worker_groups()
    {
        // thread groups have number of threads as an int
        // iterate through vector of thread groups and spawn threads and populate one worker group per thread group
        for (auto &thread_group : thread_groups)
        {
            thread_group_id_worker_map.emplace(thread_group.tg_id, std::make_shared<WorkerGroup>());
            auto worker_group = thread_group_id_worker_map.at(thread_group.tg_id);

            for (int i = 0; i < thread_group.number_of_threads; i++)
            {
                // spawn number_of_threads amount of threads and populate one worker group per thread
                auto t = std::make_unique<std::thread>([worker_group]() -> void
                                                       { worker_main(worker_group); }); // TODO: pass in lamba to exec any executable. Or we could pass in this, but its a little excessive
                set_fifo_prio(thread_group.priority, *t);
                t->detach();
                worker_group->threads.push_back(std::move(t));
            }
        }
    }

    void PreemptiveExecutor::wait_for_work(std::chrono::nanoseconds timeout)
    {
        using rclcpp::exceptions::throw_from_rcl_error;
        TRACEPOINT(rclcpp_executor_wait_for_work, timeout.count());

        // NOTE: do not remove based on cb groups (this is a major deviation from the default)
        //          We won't resize the waitset- since we disallow updating the entity set, recollecting is not required

        {
            std::lock_guard<std::mutex> guard(mutex_);

            // clear wait set
            rcl_ret_t ret = rcl_wait_set_clear(&wait_set_);
            if (ret != RCL_RET_OK)
            {
                throw_from_rcl_error(ret, "Couldn't clear wait set");
            }

            // add handles to wait on
            if (!memory_strategy_->add_handles_to_wait_set(&wait_set_))
            {
                throw std::runtime_error("Couldn't fill wait set");
            }
        }

        rcl_ret_t status = rcl_wait(&wait_set_, std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count());

        if (status == RCL_RET_WAIT_SET_EMPTY)
        {
            RCUTILS_LOG_WARN_NAMED(
                "rclcpp",
                "empty wait set received in rcl_wait(). This should never happen.");
        }
        else if (status != RCL_RET_OK && status != RCL_RET_TIMEOUT)
        {
            throw_from_rcl_error(status, "rcl_wait() failed");
        }
    }

    void PreemptiveExecutor::collect_entities()
    {
        // Is this function call needed if we're not supporting dynamic entity changes?
        // add_callback_groups_from_nodes_associated_to_executor();

        // Clear the handles from the memory strategy
        rt_memory_strategy_->clear_handles();

        // Default method collects all types of entities (subscriptions, services, clients, timers, waitables)
        bool has_invalid_weak_groups_or_nodes = rt_memory_strategy_->collect_entities(weak_groups_to_nodes_);

        if (has_invalid_weak_groups_or_nodes)
        {
            std::vector<rclcpp::CallbackGroup::WeakPtr> invalid_group_ptrs;
            for (auto pair : weak_groups_to_nodes_)
            {
                auto weak_group_ptr = pair.first;
                auto weak_node_ptr = pair.second;
                if (weak_group_ptr.expired() || weak_node_ptr.expired())
                {
                    invalid_group_ptrs.push_back(weak_group_ptr);
                }
            }
            std::for_each(
                invalid_group_ptrs.begin(), invalid_group_ptrs.end(),
                [this](rclcpp::CallbackGroup::WeakPtr group_ptr)
                {
                    if (weak_groups_to_nodes_associated_with_executor_.find(group_ptr) !=
                        weak_groups_to_nodes_associated_with_executor_.end())
                    {
                        weak_groups_to_nodes_associated_with_executor_.erase(group_ptr);
                    }
                    if (weak_groups_associated_with_executor_to_nodes_.find(group_ptr) !=
                        weak_groups_associated_with_executor_to_nodes_.end())
                    {
                        weak_groups_associated_with_executor_to_nodes_.erase(group_ptr);
                    }
                    auto callback_guard_pair = weak_groups_to_guard_conditions_.find(group_ptr);
                    if (callback_guard_pair != weak_groups_to_guard_conditions_.end())
                    {
                        auto guard_condition = callback_guard_pair->second;
                        weak_groups_to_guard_conditions_.erase(group_ptr);
                        rt_memory_strategy_->remove_guard_condition(guard_condition);
                    }
                    weak_groups_to_nodes_.erase(group_ptr);
                });
        }
    }

    void PreemptiveExecutor::spin()
    {
        // Init wait set, collect entities
        // Ensure entities are properly registered with the wait set
        {
            std::lock_guard<std::mutex> guard(mutex_);
            collect_entities();
        }

        spawn_worker_groups();

        // runs a while loop that calls wait for work
        while (rclcpp::ok(context_) && spinning.load())
        {
            wait_for_work(std::chrono::nanoseconds(-1));

            // Check if interrupt guard condition was triggered (indicates entity changes)
            // We don't support dynamic entity changes, so throw if entities were added/removed
            {
                std::lock_guard<std::mutex> guard(mutex_);
                const rcl_guard_condition_t *interrupt_gc = &interrupt_guard_condition_.get_rcl_guard_condition();
                bool interrupt_triggered = false;
                for (size_t i = 0; i < wait_set_.size_of_guard_conditions; ++i)
                {
                    if (wait_set_.guard_conditions[i] == interrupt_gc)
                    {
                        interrupt_triggered = true;
                        break;
                    }
                }
                if (interrupt_triggered)
                {
                    throw std::runtime_error(
                        "PreemptiveExecutor does not support dynamic entity changes. "
                        "Entities (nodes/callback groups) cannot be added or removed while spinning.");
                }
            }

            // TODO: Post-processing the waitresult
        }
    }
}