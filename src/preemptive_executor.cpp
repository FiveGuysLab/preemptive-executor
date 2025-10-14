#include "../include/preemptive_executor/preemptive_executor.hpp"

#include <chrono>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>


namespace preemptive_executor
{
    void set_sched_fifo_syscall(int priority){
        sched_attr attr{};
        attr.size = sizeof(attr);
        attr.sched_policy  = SCHED_FIFO;
        attr.sched_flags   = reset_on_fork ? SCHED_FLAG_RESET_ON_FORK : 0;
        attr.sched_priority= std::clamp(priority, sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(SCHED_FIFO));
        
        long ret syscall(SYS_sched_setattr, 0, &attr, 0);

        if (ret != 0){
            throw std::system_error(errno, std::system_category(), "sched_setattr syscall failed");
        }
    }

    void worker_main(ThreadGroupAttributes* thread_group, WorkerGroup* worker_group){
        //1: set timing policy
        try {
            //set SCHED FIFO prio using syscall
            set_sched_fifo_syscall(thread_group->priority);
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to set priority with SCHED FIFO: " << e.what()
                  << " (continuing with normal scheduling)\n";
        }
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