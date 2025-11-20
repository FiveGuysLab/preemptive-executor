#include <mutex>
#include <queue>
#include <thread>
#include "bundled_executable.hpp"

#define _SEM_SPIN_NS // TODO: should probably be a config instead

namespace  preemptive_executor {
    constexpr int _SEM_MAX_AT_LEAST = 50000; // TODO: How should we set this value?

    class WorkerGroup {
        class ReadyQueue { // TODO: Lock-free Q needed
            public:
                ReadyQueue();
                std::mutex mutex;
                std::queue<std::unique_ptr<BundledExecutable>> queue;
                int num_working;
        };

        protected:
            int priority;
            std::vector<std::unique_ptr<std::thread>> threads;
            rclcpp::Context::SharedPtr exec_context;
            const std::atomic_bool& exec_spinning;
            void worker_main();

        public:
            WorkerGroup(int priority_, int number_of_threads, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~WorkerGroup();
            std::counting_semaphore<_SEM_MAX_AT_LEAST> semaphore;
            ReadyQueue ready_queue;
            // Caller must first acquire the readyQ mutex
            virtual void update_prio();
    };

    class MutexGroup : public WorkerGroup {
        bool is_boosted; // protected by ready_queue.mutex

        public:
            MutexGroup(int priority_, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~MutexGroup();
            // Caller must first acquire the readyQ mutex
            virtual void update_prio() override;
    };

}