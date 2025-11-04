#ifndef PREEMPTIVE_EXECUTOR
#define PREEMPTIVE_EXECUTOR

#include <chrono>
#include <memory>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <set>
#include <vector>
#include <semaphore>
#include <climits>

#include "rclcpp/executor.hpp"
#include "rt_wait_result.hpp"
#include "rt_wait_set.hpp"
#include "bundled_executable.hpp"

#define _CORE_COUNT 16 // TODO:

namespace preemptive_executor
{
    class WorkerGroup {
        struct ReadyQueue {
            std::mutex mutex;
            std::queue<std::unique_ptr<BundledExecutable>> queue;
        };

        public:
            //constructor for WorkerGroup should take in a vector of thread ids and instantiate the semaphore to make those thread id wait on it
            WorkerGroup(): threads(std::vector<std::thread *>()), semaphore(std::make_shared<std::counting_semaphore<_CORE_COUNT>>(0)) {}
            ~WorkerGroup();
            std::vector<std::thread *> threads;
            std::shared_ptr<std::counting_semaphore<_CORE_COUNT>> semaphore;
            ReadyQueue ready_queue;
    };


    struct ThreadGroupAttributes {
        public:
            ThreadGroupAttributes(int tg_id, int number_of_threads, int priority):  tg_id(tg_id), number_of_threads(number_of_threads), priority(priority) {}
            int tg_id;
            int number_of_threads;
            int priority; //int from 1-99
    };

    class PreemptiveExecutor : protected rclcpp::Executor
    {
    public:
        RCLCPP_SMART_PTR_DEFINITIONS(PreemptiveExecutor)

        //constructor for PreemptiveExecutor
        RCLCPP_PUBLIC explicit PreemptiveExecutor(); //leaving ctor params blank for now

        RCLCPP_PUBLIC virtual ~PreemptiveExecutor();
        RCLCPP_PUBLIC size_t get_number_of_threads() const; // Why?
        RCLCPP_PUBLIC void spin() override;

        // // Timeout for getting next executable
        // std::chrono::nanoseconds next_exec_timeout_;

    protected:
        RCLCPP_PUBLIC void run(size_t this_thread_number);
        bool get_next_executable(rclcpp::AnyExecutable &any_executable, std::chrono::nanoseconds timeout = std::chrono::nanoseconds(-1));
        bool get_next_ready_executable( rclcpp::AnyExecutable &any_executable);
        void wait_for_work(std::chrono::nanoseconds timeout);

        //helper methods for preemptive executor
        void spawn_worker_groups(); //called in spin, spawns all WorkerGroups based on thread attributes
        void* get_callback_handle(const rclcpp::AnyExecutable& executable);   //get callback handle from different ROS2 callback types

        // Replacing the wait set wrappers that we'll be using
        preemptive_executor::RTWaitSet wait_set_ RCPPUTILS_TSA_GUARDED_BY(mutex_);
        std::optional<preemptive_executor::RTWaitResult<preemptive_executor::RTWaitSet>> wait_result_ RCPPUTILS_TSA_GUARDED_BY(mutex_);

    private:
        RCLCPP_DISABLE_COPY(PreemptiveExecutor);
        
        //data structures for preemptive executor
        std::unordered_map<int, WorkerGroup>thread_group_id_worker_map; 

        std::vector<ThreadGroupAttributes> thread_groups;

        //TODO: need a map bw chain id and ready set after ready set is defined 
    };

} 

#endif 
