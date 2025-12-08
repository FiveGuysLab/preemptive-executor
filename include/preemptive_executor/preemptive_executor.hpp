#ifndef PREEMPTIVE_EXECUTOR
#define PREEMPTIVE_EXECUTOR

#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <semaphore>

#include "rclcpp/executor.hpp"
#include "rclcpp/executors/multi_threaded_executor.hpp"
#include "preemptive_executor/rt_allocator_memory_strategy.hpp"
#include "preemptive_executor/worker_group.hpp"
#include "preemptive_executor/yaml_parser.hpp"

#define MAX_FIFO_PRIO 99
#define CORE_COUNT 16 //TODO: make this a config

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
        RCLCPP_PUBLIC void add_node(rclcpp::Node::SharedPtr node); 

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
        void classify_callback_groups();
        void stop_non_rt_executor();

        rclcpp::memory_strategy::MemoryStrategy::WeakCallbackGroupsToNodesMap pending_weak_groups_to_nodes_;
        std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr, rclcpp::node_interfaces::NodeBaseInterface::SharedPtr>> non_rt_callback_groups;
        std::vector<std::thread> non_rt_threads;
        std::shared_ptr<rclcpp::executors::MultiThreadedExecutor> non_rt_executor;
        bool non_rt_callback_groups_spawned = false;
    };
} 

#endif 
