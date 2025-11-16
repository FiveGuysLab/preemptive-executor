#include <chrono>
#include <cstddef>
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include "preemptive_executor/preemptive_executor.hpp"
#include "preemptive_executor/bundled_subscription.hpp"
#include "preemptive_executor/bundled_timer.hpp"
#include <linux/sched.h>
#include <vector>


namespace preemptive_executor
{
    PreemptiveExecutor::PreemptiveExecutor(const rclcpp::ExecutorOptions & options, memory_strategy::RTMemoryStrategy::SharedPtr rt_memory_strategy)
    :   Executor(options), rt_memory_strategy_(rt_memory_strategy)
    {
        if (memory_strategy_ != rt_memory_strategy_) {
            RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "rt_memory_strategy must be a derivation of options.memory_strategy");
        }
    }

    void PreemptiveExecutor::spawn_worker_groups() {
        // thread groups have number of threads as an int 
        // iterate through vector of thread groups and spawn threads and populate one worker group per thread group
        for(auto& thread_group : thread_groups){
            thread_group_id_worker_map.emplace(thread_group.tg_id, std::make_unique<WorkerGroup>(thread_group.priority, thread_group.number_of_threads));
        }
    }

    void PreemptiveExecutor::wait_for_work(std::chrono::nanoseconds timeout)
    {
        //TODO: @viraj add remove_null_handles() to wait_for_work() 

        using rclcpp::exceptions::throw_from_rcl_error;
        TRACEPOINT(rclcpp_executor_wait_for_work, timeout.count());

        // NOTE: do not remove based on cb groups (this is a major deviation from the default)
        //          We won't resize the waitset- since we disallow updating the entity set, recollecting is not required

        {
            std::lock_guard<std::mutex> guard(mutex_);

            // clear wait set
            rcl_ret_t ret = rcl_wait_set_clear(&wait_set_);
            if (ret != RCL_RET_OK) {
                throw_from_rcl_error(ret, "Couldn't clear wait set");
            }

            // add handles to wait on
            if (!memory_strategy_->add_handles_to_wait_set(&wait_set_)) {
                throw std::runtime_error("Couldn't fill wait set");
            }
        }
        
        rcl_ret_t status = rcl_wait(&wait_set_, std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count());

        if (status == RCL_RET_WAIT_SET_EMPTY) {
            RCUTILS_LOG_WARN_NAMED(
                "rclcpp",
                "empty wait set received in rcl_wait(). This should never happen.");
        } else if (status != RCL_RET_OK && status != RCL_RET_TIMEOUT) {
            throw_from_rcl_error(status, "rcl_wait() failed");
        }
    }

    void PreemptiveExecutor::populate_ready_queues() {
        if (!rt_memory_strategy_) {
            return;
        }

        if (thread_group_id_worker_map.empty()) {
            return;
        }


        std::unordered_map<int, std::vector<std::unique_ptr<BundledExecutable>>> bundles_by_tgid;

        auto resolve_tgid = [this](const BundledExecutable & bundle) -> int {
            (void)bundle;
            //TODO: Resolve TGID from callback metadata (chain ID, callback group mapping, etc.)
            //using first available worker group as fallback. TODO: improve this.
            return thread_group_id_worker_map.begin()->first;
        };

        auto emplace_bundle = [&bundles_by_tgid](int tgid, std::unique_ptr<BundledExecutable> bundle) {
            if (!bundle) {
                return;
            }
            bundles_by_tgid[tgid].push_back(std::move(bundle));
        };

        std::vector<rclcpp::SubscriptionBase::SharedPtr> ready_subscriptions;
        std::vector<rclcpp::TimerBase::SharedPtr> ready_timers;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            rt_memory_strategy_->take_ready_handles(weak_groups_to_nodes_, ready_subscriptions, ready_timers);
        }
        for (auto & subscription : ready_subscriptions) {
            auto bundle = preemptive_executor::take_and_bundle(subscription);
            if (!bundle) {
                continue;
            }

            auto target_tgid = resolve_tgid(*bundle);
            emplace_bundle(target_tgid, std::move(bundle));
        }

        for (auto & timer : ready_timers) {
            auto bundle = preemptive_executor::BundledTimer::take_and_bundle(timer);
            if (!bundle) {
                continue;
            }

            auto target_tgid = resolve_tgid(*bundle);
            emplace_bundle(target_tgid, std::move(bundle));
        }

        for (auto & [tgid, bundles] : bundles_by_tgid) {
            if (bundles.empty()) {
                continue;
            }

            auto worker_it = thread_group_id_worker_map.find(tgid);
            if (worker_it == thread_group_id_worker_map.end()) {
                throw std::runtime_error("Unknown thread group id " + std::to_string(tgid) +
                    " when dispatching rexady bundles");
            }
            auto & worker_group = worker_it->second;

            {
                std::lock_guard<std::mutex> guard(worker_group->ready_queue.mutex);
                for (auto & bundle : bundles) {
                    worker_group->ready_queue.queue.push(std::move(bundle));
                }
                worker_group->update_prio();
            }
            worker_group->semaphore.release(static_cast<std::ptrdiff_t>(bundles.size()));
        }

        // TODO: services, clients, and waitables will be bundled once the dispatching story is defined.
    }

    void PreemptiveExecutor::spin() {
        // TODO: Init wait set, collect entities

        spawn_worker_groups();

        //runs a while loop that calls wait for work
        while(rclcpp::ok(context_) && spinning.load()) {
            // TODO: Add a check for entity recollection. We don't support it, so throw if that state changes.

            wait_for_work(std::chrono::nanoseconds(-1));

            populate_ready_queues();
        }
    }
}
