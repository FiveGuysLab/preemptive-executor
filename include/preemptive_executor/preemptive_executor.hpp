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
#include "bundled_executable.hpp"
#include "rt_allocator_memory_strategy.hpp"

#define MAX_FIFO_PRIO 99
#define _CORE_COUNT 16 // TODO: this cannot be a config, should we just set a much higher value? There's little disadvantage
#define _SEM_SPIN_NS // TODO: should probably be a config instead

namespace preemptive_executor
{
    void set_fifo_prio(int priority, std::thread& t);

    class WorkerGroup {
        class ReadyQueue {
            public:
                ReadyQueue();
                std::mutex mutex;
                std::queue<std::unique_ptr<BundledExecutable>> queue;
                int num_working;
        };

        protected:
            int priority;

        public:
            WorkerGroup(int priority_);
            virtual ~WorkerGroup();
            std::vector<std::unique_ptr<std::thread>> threads;
            std::shared_ptr<std::counting_semaphore<_CORE_COUNT>> semaphore;
            ReadyQueue ready_queue;
            // Caller must first acquire the readyQ mutex
            virtual void update_prio();
    };

    class MutexGroup : public WorkerGroup { // NOTE: We don't enforce num threads for this class, but it MUST be == 1
        bool is_boosted; // protected by ready_queue.mutex

        public:
            MutexGroup(int priority_);
            virtual ~MutexGroup();
            // Caller must first acquire the readyQ mutex
            virtual void update_prio() override;
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
        RCLCPP_PUBLIC explicit PreemptiveExecutor(const rclcpp::ExecutorOptions & options, memory_strategy::RTMemoryStrategy::SharedPtr rt_memory_strategy);

        RCLCPP_PUBLIC virtual ~PreemptiveExecutor(); // TODO:
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
        void populate_ready_queues(); // bundles ready work items and schedules them on worker groups

        // Replacing the wait set wrappers that we'll be using
        memory_strategy::RTMemoryStrategy::SharedPtr
        rt_memory_strategy_ RCPPUTILS_TSA_PT_GUARDED_BY(mutex_);

    private:
        RCLCPP_DISABLE_COPY(PreemptiveExecutor)
        
        //data structures for preemptive executor
        std::unordered_map<int, std::shared_ptr<WorkerGroup>>thread_group_id_worker_map; 

        std::vector<ThreadGroupAttributes> thread_groups;

        //TODO: need a map bw chain id and ready set after ready set is defined     
    };

} 

#endif 
