#include <chrono>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "preemptive_executor.hpp"
#include <linux/sched.h>


namespace preemptive_executor
{
    void set_fifo_prio(int priority, std::thread& t){
        const auto param = sched_param{priority};
        pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
    }

    void worker_main(WorkerGroup& worker_group){

        //1: set timing policy // NOTE: Handled by dispatcher
        //2: register with thread group // NOTE: Registration handled by dispatcher

        while (true) {
            //3: wait on worker group semaphore
            worker_group.semaphore->acquire();

            if (!rclcpp::ok){
                break;
            }

            //4: acquire ready queue mutex and 5: pop from ready queue
            rclcpp::AnyExecutable next_executable;
            bool has_executable = false;
            {
               worker_group.ready_queue.mutex.lock();
                if (!worker_group.ready_queue.queue.empty()) {
                    next_executable = worker_group.ready_queue.queue.front(); // ref or ptr
                    worker_group.ready_queue.queue.pop();
                    has_executable = true; // dont neeed
                }
            }

            //6: unlock ready queue mutex 
            worker_group.ready_queue.mutex.unlock();

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            if (has_executable) {
                // TODO: This is a non-static class member. I think our executor should pass the workers a lamba to call this
                PreemptiveExecutor::execute_any_executable(next_executable);
            }
        }
    }

    // NOTE: Can't use this. No way to make these types work. Needs more thought.
    // void* PreemptiveExecutor::get_callback_handle(const rclcpp::AnyExecutable& executable) {
    //     //check which callback type is active 
    //     if (executable.subscription) {
    //         return executable.subscription->get_subscription_handle();
    //     } else if (executable.timer) {
    //         return executable.timer->get_timer_handle();
    //     } else if (executable.service) {
    //         return executable.service->get_service_handle();
    //     } else if (executable.client) {
    //         return executable.client->get_client_handle();
    //     } else if (executable.waitable) {
    //         return executable.waitable->get_handle();
    //     } else {
    //         return nullptr;
    //     }
    // }

    void PreemptiveExecutor::spawn_worker_groups(){
        // thread groups have number of threads as an int 
        // iterate through vector of thread groups and spawn threads and populate one worker group per thread group
        for(auto& thread_group : thread_groups){
            thread_group_id_worker_map.emplace(thread_group.tg_id, std::move(WorkerGroup()));
            auto& worker_group = thread_group_id_worker_map.at(thread_group.tg_id);

            for (int i = 0; i < thread_group.number_of_threads; i++){
                //spawn number_of_threads amount of threads and populate one worker group per thread
                auto t = new std::thread([&worker_group]() -> void {worker_main(worker_group);}); // pass in lamba to exec any executable. Or we could pass in this, but its a little excessive
                worker_group.threads.push_back(t);
                set_fifo_prio(thread_group.priority, *t);
                t->detach();
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

    void PreemptiveExecutor::spin() {
        spawn_worker_groups();

        //runs a while loop that calls wait for work
        while(rclcpp::ok(context_) && spinning.load()) {
            // TODO: Add a check for entity recollection. We don't support it, so throw if that state changes.

            rclcpp::AnyExecutable any_executable;
            wait_for_work(std::chrono::nanoseconds(-1));

            // TODO: Post-processing the waitresult
        }
    }
}