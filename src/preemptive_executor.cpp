#include <chrono>
#include <cstddef>
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <linux/sched.h>
#include <vector>
#include "preemptive_executor/preemptive_executor.hpp"
#include "preemptive_executor/bundled_subscription.hpp"
#include "preemptive_executor/bundled_timer.hpp"
#include "preemptive_executor/callback_registry.hpp"


namespace preemptive_executor
{
    PreemptiveExecutor::PreemptiveExecutor(
        const rclcpp::ExecutorOptions & options, memory_strategy::RTMemoryStrategy::SharedPtr rt_memory_strategy,
        const std::unordered_map<std::string, userChain>& user_chains
    ):   Executor(options), rt_memory_strategy_(rt_memory_strategy), user_chains(user_chains)
    {
        if (memory_strategy_ != rt_memory_strategy_) {
            RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "rt_memory_strategy must be a derivation of options.memory_strategy");
        }
    }

    PreemptiveExecutor::~PreemptiveExecutor() {};

    void PreemptiveExecutor::spawn_worker_groups() {
        // thread groups have number of threads as an int 
        // iterate through vector of thread groups and spawn threads and populate one worker group per thread group
        for(auto& pair : (*thread_groups)){
            auto& thread_group = pair.second;
            std::unique_ptr<WorkerGroup> wg = nullptr;
            if (thread_group.is_mutex_group) {
                wg = std::make_unique<MutexGroup>(thread_group.priority, context_, spinning);
            } else {
                wg = std::make_unique<WorkerGroup>(thread_group.priority, thread_group.number_of_threads, context_, spinning);
            }
            thread_group_id_worker_map.emplace(thread_group.tg_id, std::move(wg));
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

            // TODO: There are unidentified subscriptions that show up here that were not in the timing info
            if (!callback_handle_to_threadgroup_id->contains(bundle->get_raw_handle())) {
                std::cout << "Warning: Subscription callback handle not found in timing info." << std::endl;
                continue;
            }
            auto target_tgid = callback_handle_to_threadgroup_id->at(bundle->get_raw_handle());
            emplace_bundle(target_tgid, std::move(bundle));
        }

        for (auto & timer : ready_timers) {
            auto bundle = preemptive_executor::BundledTimer::take_and_bundle(timer);
            if (!bundle) {
                continue;
            }

            auto target_tgid = callback_handle_to_threadgroup_id->at(bundle->get_raw_handle());
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

            auto ok = worker_group->ready_queue.queue.enqueue_bulk(std::make_move_iterator(bundles.begin()), bundles.size());
            if (!ok) {
                throw std::runtime_error("Failed to enqueue ready bundles to worker group (out of memory) " + std::to_string(tgid));
            }

            worker_group->ready_queue.num_pending.fetch_add(static_cast<int>(bundles.size()));
            worker_group->update_prio();
            worker_group->semaphore.release(static_cast<std::ptrdiff_t>(bundles.size()));
        }

        // TODO: services, clients, and waitables will be bundled once the dispatching story is defined.
    }

    void PreemptiveExecutor::load_timing_info() {
        auto& registry = CallbackRegistry::get_instance(weak_groups_to_nodes_, user_chains);
        TimingExport result = registry.callback_threadgroup_allocation();
        std::swap(callback_handle_to_threadgroup_id, result.callback_handle_to_threadgroup_id);
        std::swap(thread_groups, result.threadgroup_attributes);
    }

    void PreemptiveExecutor::spin() {
        if (spinning.exchange(true)) {
            throw std::runtime_error("spin_some() called while already spinning");
        }
        RCPPUTILS_SCOPE_EXIT(this->spinning.store(false); );

        /**
         * TODO: We currently don't support many things the default executor does. We could consider
         * making this private inheritance and then just expose what we do support.
         * 
         * TODO: We should support non-rt chains- ie, callback_handle_to_threadgroup_id->at(...) fails
         * But we need some custom mutex handling for those, so its not yet implemented.
         * 
         * TODO: We need to test how shutdown behaves with this executor.
         *  */

        load_timing_info();

        spawn_worker_groups();

        //runs a while loop that calls wait for work
        while(rclcpp::ok(context_) && spinning.load()) {
            wait_for_work(std::chrono::nanoseconds(-1));

            populate_ready_queues();
        }
    }
}
