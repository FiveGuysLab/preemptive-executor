#include <atomic>
#include <thread>
#include "bundled_executable.hpp"
#include "concurrentqueue.h"

namespace  preemptive_executor {
    constexpr int _SEM_MAX_AT_LEAST = 50000; // TODO: How should we set this value?

    class WorkerGroup {
        class ReadyQueue {
          public:
            ReadyQueue();
            moodycamel::ConcurrentQueue<std::unique_ptr<BundledExecutable>> queue;
            std::atomic<int> num_pending;
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
            virtual void update_prio();
    };

    class MutexGroup : public WorkerGroup {
        std::atomic<bool> is_boosted;

        public:
            MutexGroup(int priority_, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~MutexGroup();
            virtual void update_prio() override;
    };

}