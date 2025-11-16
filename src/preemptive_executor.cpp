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
    void set_fifo_prio(int priority, std::thread& t){
        const auto param = sched_param{priority};
        pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
    }

    void worker_main(std::shared_ptr<WorkerGroup> worker_group){

        // 1: set timing policy // NOTE: Handled by dispatcher
        // 2: register with thread group // NOTE: Registration handled by dispatcher

        while (true) {
            // 3: wait on worker group semaphore
            worker_group->semaphore->acquire();

            if (!rclcpp::ok()){ // TODO: We didn't pass in the context, so this does nothing
                break;
            }

            // 4: acquire ready queue mutex and 5: pop from ready queue
            std::unique_ptr<BundledExecutable> exec = nullptr;
            {
                auto& rq = worker_group->ready_queue;
                std::lock_guard<std::mutex> guard(rq.mutex);

                if (rq.queue.empty() || rq.queue.front() == nullptr) {
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
        if (memory_strategy_ != rt_memory_strategy_) {
            RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "rt_memory_strategy must be a derivation of options.memory_strategy");
        }
    }

    void PreemptiveExecutor::spawn_worker_groups() {
        // thread groups have number of threads as an int
        // iterate through vector of thread groups and spawn threads and populate one worker group per thread group
        for(auto& thread_group : thread_groups){
            thread_group_id_worker_map.emplace(thread_group.tg_id, std::make_shared<WorkerGroup>());
            auto worker_group = thread_group_id_worker_map.at(thread_group.tg_id);

            for (int i = 0; i < thread_group.number_of_threads; i++){
                //spawn number_of_threads amount of threads and populate one worker group per thread
                auto t = std::make_unique<std::thread>([worker_group]() -> void {worker_main(worker_group);}); // TODO: pass in lamba to exec any executable. Or we could pass in this, but its a little excessive
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

    void PreemptiveExecutor::collect_entities()
    {
        // First, call the base executor's collect_entities() to maintain state and update the wait set
        Executor::collect_entities();

        // Additionally, we need to populate the memory strategy's handle vectors
        // so that memory_strategy_->add_handles_to_wait_set() can work properly.
        // The memory strategy's collect_entities() requires a WeakCallbackGroupsToNodesMap.


        rclcpp::memory_strategy::MemoryStrategy::WeakCallbackGroupsToNodesMap weak_groups_to_nodes;

        // Collect unique callback groups from current_collection_
        std::set<rclcpp::CallbackGroup::WeakPtr, std::owner_less<rclcpp::CallbackGroup::WeakPtr>> unique_groups;

        for (const auto &[handle, entry] : current_collection_.subscriptions)
        {
            unique_groups.insert(entry.callback_group);
        }
        for (const auto &[handle, entry] : current_collection_.timers)
        {
            unique_groups.insert(entry.callback_group);
        }
        // TODO: Add support for services, clients, and waitables

        // For each callback group, try to find its associated node


        for (const auto &weak_group : unique_groups)
        {
            auto group = weak_group.lock();
            if (!group)
            {
                continue;
            }

            // For now, we'll use an empty node - the memory strategy should still work
            // as it mainly uses callback groups to collect handles
            rclcpp::node_interfaces::NodeBaseInterface::WeakPtr associated_node;
            weak_groups_to_nodes[weak_group] = associated_node;
        }

        // Call the memory strategy's collect_entities to populate its handle vectors
        // This will allow memory_strategy_->add_handles_to_wait_set() to work properly
        if (!weak_groups_to_nodes.empty())
        {
            rt_memory_strategy_->collect_entities(weak_groups_to_nodes);
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
            // Check for entity recollection. We don't support dynamic entity changes,
            // so throw if that state changes (e.g., if nodes/callback groups are added/removed)
            if (entities_need_rebuild_.load())
            {
                throw std::runtime_error(
                    "PreemptiveExecutor does not support dynamic entity changes. "
                    "Entities (nodes/callback groups) cannot be added or removed while spinning.");
            }

            wait_for_work(std::chrono::nanoseconds(-1));

            // TODO: Post-processing the waitresult
        }
    }
}