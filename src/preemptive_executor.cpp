#include "preemptive_executor/preemptive_executor.hpp"

#include <rclcpp/memory_strategies.hpp>
#include <rclcpp/executors/executor.hpp>

#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;

namespace preemptive_executor
{

    PreemptiveExecutor::PreemptiveExecutor()
        : rclcpp::Executor()
    {
        // Initialize wait set
        wait_set_ = rcl_get_zero_initialized_wait_set();

        // Use the same default memory strategy that the base Executor uses
        memory_strategy_ = rclcpp::memory_strategies::create_default_strategy();
        rcl_allocator_t allocator = memory_strategy_->get_allocator();
    }

    PreemptiveExecutor::~PreemptiveExecutor()
    {
        // Finalize wait set
        if (rcl_wait_set_fini(&wait_set_) != RCL_RET_OK)
        {
            RCLCPP_ERROR(rclcpp::get_logger("rclcpp"), "Couldn't finalize wait set");
        }
    }

    rcl_wait_set_t *PreemptiveExecutor::get_wait_set_ptr() const
    {
        return &wait_set_;
    }

    void PreemptiveExecutor::wait_for_work(std::chrono::nanoseconds timeout)
    {

        // Clear wait set
        rcl_ret_t ret = rcl_wait_set_clear(&wait_set_);
        if (ret != RCL_RET_OK)
        {
            throw std::runtime_error("Couldn't clear wait set");
        }

        // Resize wait set based on current entities
        ret = rcl_wait_set_resize(
            &wait_set_,
            memory_strategy_->number_of_ready_subscriptions(),
            memory_strategy_->number_of_guard_conditions(),
            memory_strategy_->number_of_ready_timers(),
            memory_strategy_->number_of_ready_clients(),
            memory_strategy_->number_of_ready_services(),
            memory_strategy_->number_of_ready_events());

        if (ret != RCL_RET_OK)
        {
            throw std::runtime_error("Couldn't resize wait set");
        }

        // Add handles to wait set
        if (!memory_strategy_->add_handles_to_wait_set(&wait_set_))
        {
            throw std::runtime_error("Couldn't fill wait set");
        }

        // Wait for work
        int64_t timeout_ns = timeout.count();
        if (timeout_ns < 0)
        {
            timeout_ns = 0;
        }

        ret = rcl_wait(&wait_set_, timeout_ns);
        if (ret == RCL_RET_TIMEOUT)
        {
            return;
        }
        else if (ret != RCL_RET_OK)
        {
            throw std::runtime_error("rcl_wait() failed");
        }

        // Remove null handles
        memory_strategy_->remove_null_handles(&wait_set_);
    }

} // namespace preemptive_executor

namespace preemptive_executor
{
    void worker_main(ThreadGroup* thread_group, WorkerGroup* worker_group){
        //TODO: 1: set timing policy
        //2: register with thread group
        while (true) {
            //3: wait on worker group semaphore
            worker_group->semaphore->acquire();

            if (!rclcpp::ok){
                break;
            }

            //4: acquire ready queue mutex and 5: pop from ready queue
            rclcpp::AnyExecutable next_executable;
            bool has_executable = false;
            {
               worker_group->ready_queue.mutex.lock();
                if (!worker_group->ready_queue.queue.empty()) {
                    next_executable = worker_group->ready_queue.queue.front(); // ref or ptr
                    worker_group->ready_queue.queue.pop();
                    has_executable = true; // dont neeed
                }
            }

            //6: unlock ready queue mutex 
            worker_group->ready_queue.mutex.unlock();

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            if (has_executable) {
                // what to do here?
                PreemptiveExecutor::execute_any_executable(next_executable);
            }
        }
    }
    void* PreemptiveExecutor::get_callback_handle(const rclcpp::AnyExecutable& executable) {
        //check which callback type is active 
        if (executable.subscription) {
            return executable.subscription->get_subscription_handle();
        } else if (executable.timer) {
            return executable.timer->get_timer_handle();
        } else if (executable.service) {
            return executable.service->get_service_handle();
        } else if (executable.client) {
            return executable.client->get_client_handle();
        } else if (executable.waitable) {
            return executable.waitable->get_handle();
        } else {
            return nullptr;
        }
    }

    void PreemptiveExecutor::spawn_worker_groups(){
        //thread groups have number of threads as an int 
        // iterate through vector of thread groups and spawn threads and populate one worker group per thread group
        for(auto& thread_group : thread_groups){
            WorkerGroup* worker_group = new WorkerGroup();
            std::vector<pid_t> thread_ids;

            for (int i = 0; i < thread_group.number_of_threads; i++){
                //spawn number_of_threads amount of threads and populate one worker group per thread
                std::thread([this, thread_group, worker_group]() -> void {worker_main(thread_group, worker_group);}).detach();
                //get thread id
                std::thread::id thread_id = std::this_thread::get_id();
                worker_group->thread_ids.push_back(thread_id);
            }
            thread_group_id_worker_map[thread_group.tg_id] = worker_group;
        }
    }

    void PreemptiveExecutor::spin() {
        spawn_worker_groups();

        //runs a while loop that calls wait for work
        while(rclcpp::ok(this->context_)){
            rclcpp::AnyExecutable any_executable;
            wait_for_work(std::chrono::nanoseconds(-1));
            
            // Get callback handle using helper method
            void* callback_handle = get_callback_handle(any_executable);
            if (callback_handle && callback_id_worker_group_map.find(callback_handle) != callback_id_worker_group_map.end()) {
                callback_id_worker_group_map[callback_handle]->semaphore->release();
            }
        }
    }
}
