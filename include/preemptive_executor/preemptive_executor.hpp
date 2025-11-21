#ifndef PREEMPTIVE_EXECUTOR
#define PREEMPTIVE_EXECUTOR

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <semaphore>

#include "rclcpp/executor.hpp"
#include "preemptive_executor/rt_allocator_memory_strategy.hpp"
#include "preemptive_executor/worker_group.hpp"
#include "preemptive_executor/yaml_parser.hpp"

#define MAX_FIFO_PRIO 99

namespace preemptive_executor
{
    struct ThreadGroupAttributes {
        public:
            ThreadGroupAttributes(int tg_id, int number_of_threads, int priority, bool is_mutex_group):  tg_id(tg_id), number_of_threads(number_of_threads), priority(priority), is_mutex_group(is_mutex_group) {}
            int tg_id;
            int number_of_threads;
            int priority; //int from 1-99
            bool is_mutex_group;
    };

    class PreemptiveExecutor : public rclcpp::Executor
    {
    public:
        RCLCPP_SMART_PTR_DEFINITIONS(PreemptiveExecutor)

        //constructor for PreemptiveExecutor
        RCLCPP_PUBLIC explicit PreemptiveExecutor(
            const rclcpp::ExecutorOptions & options,
            memory_strategy::RTMemoryStrategy::SharedPtr rt_memory_strategy,
            const std::unordered_map<std::string, userChain>& user_chains
        );

        RCLCPP_PUBLIC virtual ~PreemptiveExecutor(); // TODO:
        RCLCPP_PUBLIC void spin() override;
        static std::atomic<uint64_t> SEM_SPIN_NS;
        static std::atomic<bool> PROFILING_MODE;

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
        std::unique_ptr<std::unordered_map<void*, int>> callback_handle_to_threadgroup_id;
        std::unique_ptr<std::unordered_map<int, ThreadGroupAttributes>> thread_groups;
        const std::unordered_map<std::string, userChain>& user_chains;
        void load_timing_info();
        
        // constants for profiling
        uint16_t FUNCTION_TIMING_RUN_THERESHOLD = 1000;
        uint16_t FUNCTION_TIMING_STARTUP_THRESHOLD = 200;

        // temp variables for profiling
        uint16_t function_timing_iterations_ = 0;
        std::vector<uint64_t> overhead_ns_vector_;

        uint64_t calculate_spin_overhead(std::vector<uint64_t> &vector, double percentile) {
            int n = vector.size();
            std::sort(vector.begin(), vector.end());
            double rank = percentile / 100.0 * n;
            int index = (int)std::ceil(rank) - 1;

            if (index > n - 1) {
                index = n - 1;
            }

            return vector[index];
        }
    };

} 

#endif 
