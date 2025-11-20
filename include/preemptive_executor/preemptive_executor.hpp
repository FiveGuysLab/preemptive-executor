#ifndef PREEMPTIVE_EXECUTOR
#define PREEMPTIVE_EXECUTOR

#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <semaphore>

#include "rclcpp/executor.hpp"
#include "rt_allocator_memory_strategy.hpp"
#include "worker_group.hpp"

#define MAX_FIFO_PRIO 99

namespace preemptive_executor
{
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
        RCLCPP_PUBLIC explicit PreemptiveExecutor(const rclcpp::ExecutorOptions & options, memory_strategy::RTMemoryStrategy::SharedPtr rt_memory_strategy);

        RCLCPP_PUBLIC virtual ~PreemptiveExecutor(); // TODO:
        RCLCPP_PUBLIC void spin() override;

    protected:
        //helper methods for preemptive executor
        void spawn_worker_groups(); //called in spin, spawns all WorkerGroups based on thread attributes
        void populate_ready_queues(); // bundles ready work items and schedules them on worker groups

        // Replacing the wait set wrappers that we'll be using
        memory_strategy::RTMemoryStrategy::SharedPtr
        rt_memory_strategy_ RCPPUTILS_TSA_PT_GUARDED_BY(mutex_);

    private:
        RCLCPP_DISABLE_COPY(PreemptiveExecutor)
        
        //data structures for preemptive executor
        std::unordered_map<int, std::unique_ptr<WorkerGroup>>thread_group_id_worker_map; 

        std::vector<ThreadGroupAttributes> thread_groups;
    };

} 

#endif 
