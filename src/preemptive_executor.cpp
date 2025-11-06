#include <chrono>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "preemptive_executor/preemptive_executor.hpp"
#include "preemptive_executor/bundled_subscription.hpp"
#include <linux/sched.h>


namespace preemptive_executor
{
    void set_fifo_prio(int priority, std::thread& t){
        const auto param = sched_param{priority};
        pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
    }

    void worker_main(std::shared_ptr<WorkerGroup> worker_group){

        //1: set timing policy // NOTE: Handled by dispatcher
        //2: register with thread group // NOTE: Registration handled by dispatcher

        while (true) {
            //3: wait on worker group semaphore
            worker_group->semaphore->acquire();

            if (!rclcpp::ok()){ // TODO: We didn't pass in the context, so this does nothing
                break;
            }

            //4: acquire ready queue mutex and 5: pop from ready queue
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

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            exec->run();
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
    TRACETOOLS_TRACEPOINT(rclcpp_executor_wait_for_work, timeout.count());

    // Clear any previous wait result
    this->wait_result_.reset();

    // we need to make sure that callback groups don't get out of scope
    // during the wait. As in jazzy, they are not covered by the DynamicStorage,
    // we explicitly hold them here as a bugfix
    std::vector<rclcpp::CallbackGroup::SharedPtr> cbgs;

    {
        std::lock_guard<std::mutex> guard(mutex_);

        if (this->entities_need_rebuild_.exchange(false) || current_collection_.empty()) {
        this->collect_entities();
        }

        auto callback_groups = this->collector_.get_all_callback_groups();
        cbgs.resize(callback_groups.size());
        for(const auto & w_ptr : callback_groups) {
        auto shr_ptr = w_ptr.lock();
        if(shr_ptr) {
            cbgs.push_back(std::move(shr_ptr));
        }
        }
    }

    this->wait_result_.emplace(wait_set_.wait(timeout));

    // drop references to the callback groups, before trying to execute anything
    cbgs.clear();

    if (!this->wait_result_ || this->wait_result_->kind() == rclcpp::WaitResultKind::Empty) {
        RCUTILS_LOG_WARN_NAMED(
        "rclcpp",
        "empty wait set received in wait(). This should never happen.");
    } else {
        if (this->wait_result_->kind() == rclcpp::WaitResultKind::Ready && current_notify_waitable_) {
        auto & rcl_wait_set = this->wait_result_->get_wait_set().get_rcl_wait_set();
        if (current_notify_waitable_->is_ready(rcl_wait_set)) {
            current_notify_waitable_->execute(current_notify_waitable_->take_data());
        }
        }
    }
    }

    void PreemptiveExecutor::rt_wait_return() {
        //check if wait_result_ exists and is Ready
        if (!wait_result_ || wait_result_->kind() != rclcpp::WaitResultKind::Ready) {
            return;
        }

        if (thread_group_id_worker_map.empty()) {
            return;
        }

        auto & wait_set = wait_result_->get_wait_set();
        auto & rcl_wait_set = wait_set.get_rcl_wait_set();

        //helper function to resolve thread group ID for a bundle
        //TODO: Implement proper bundle -> tgid resolution (e.g., via chain IDs @rohit)
        auto resolve_tgid = [this](const std::shared_ptr<rclcpp::SubscriptionBase>& subscription) -> int {
            //TODO: Resolve tgid from subscription metadata (e.g., callback group, chain ID, etc.)
            //using first available worker group as fallback
            return thread_group_id_worker_map.begin()->first;
        };

        auto resolve_tgid_timer = [this](const std::shared_ptr<rclcpp::TimerBase>& timer) -> int {
            //TODO: Resolve tgid from timer metadata (e.g., callback group, chain ID, etc.)
            //using first available worker group as fallback
            return thread_group_id_worker_map.begin()->first;
        };

        //collect bundles grouped by thread group ID (keeping this here as we don't need it globally in executor)
        std::unordered_map<int, std::vector<std::unique_ptr<BundledExecutable>>> bundles_by_tgid;

        //subscriptions
        for (size_t i = 0; i < wait_set.size_of_subscriptions(); ++i) {
            if (rcl_wait_set.subscriptions[i] != nullptr) {
                auto subscription = wait_set.subscriptions(i);
                if (subscription) {
                    auto bundle = preemptive_executor::take_and_bundle(subscription);
                    if (bundle) {
                        // Resolve thread group ID for this bundle
                        int tgid = resolve_tgid(subscription);
                        bundles_by_tgid[tgid].push_back(std::move(bundle));
                    }
                }
            }
        }

        //timers
        for (size_t i = 0; i < wait_set.size_of_timers(); ++i) {
            if (rcl_wait_set.timers[i] != nullptr) {
                auto timer = wait_set.timers(i);
                if (timer) {
                    auto bundle = preemptive_executor::BundledTimer::take_and_bundle(timer);
                    if (bundle) {
                        //resolve thread group ID for this bundle
                        int tgid = resolve_tgid_timer(timer);
                        bundles_by_tgid[tgid].push_back(std::move(bundle));
                    }
                }
            }
        }

        //enqueue all bundles for each worker group with a single mutex acquisition per group
        for (auto & [tgid, bundles] : bundles_by_tgid) {
            if (bundles.empty()) {
                continue;
            }

            auto it = thread_group_id_worker_map.find(tgid);
            if (it == thread_group_id_worker_map.end()) {
                //skip if worker group doesn't exist for this tgid
                continue;
            }

            auto & worker_group = it->second;
            {
                std::lock_guard<std::mutex> guard(worker_group.ready_queue.mutex);
                for (auto & bundle : bundles) {
                    worker_group.ready_queue.queue.push(std::move(bundle));
                }
            }
            worker_group.semaphore->release(static_cast<ptrdiff_t>(bundles.size()));
        }

        //TODO: services, clients, and waitables after we figure out what to do with them 
    }

    void PreemptiveExecutor::spin() {
        spawn_worker_groups();

        //runs a while loop that calls wait for work
        while(rclcpp::ok(context_) && spinning.load()) {
            // TODO: Add a check for entity recollection. We don't support it, so throw if that state changes.

            rclcpp::AnyExecutable any_executable;
            wait_for_work(std::chrono::nanoseconds(-1));

            //process wait result and enqueue bundled executables
            rt_wait_return();
        }
    }
}