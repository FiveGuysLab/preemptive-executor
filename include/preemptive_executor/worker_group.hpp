#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <semaphore>

#include "bundled_executable.hpp"
#include "concurrentqueue.h"
#include "rclcpp/context.hpp"

#define _SEM_SPIN_NS // TODO: should probably be a config instead

namespace  preemptive_executor {
    constexpr int _SEM_MAX_AT_LEAST = 50000; // TODO: How should we set this value?

    class WorkerGroupBase {
        public:
            virtual ~WorkerGroupBase();
            std::counting_semaphore<_SEM_MAX_AT_LEAST> semaphore;
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) = 0;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) = 0;
            virtual void configure_threads();

        protected:
            WorkerGroupBase(int priority_, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);

            virtual void update_prio();
            virtual std::unique_ptr<BundledExecutable> take_next_ready_executable() = 0;

            void worker_main();

            int priority;
            std::vector<std::unique_ptr<std::thread>> threads;
            rclcpp::Context::SharedPtr exec_context;
            const std::atomic_bool& exec_spinning;
    };

    class WorkerGroup : public WorkerGroupBase {
        class ReadyQueue {
            public:
                ReadyQueue();
                moodycamel::ConcurrentQueue<std::unique_ptr<BundledExecutable>> queue;
                std::atomic<int> num_pending;
        };

        public:
            WorkerGroup(int priority_, int number_of_threads, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~WorkerGroup();
            virtual void update_prio() override;
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) override;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) override;
            void configure_threads() override;

        protected:
            std::unique_ptr<BundledExecutable> take_next_ready_executable() override;
            ReadyQueue ready_queue;
    };

    class NonPrioWorkerGroup : public WorkerGroupBase {
        class ReadyVector {
            public:
                ReadyVector() = default;
                std::mutex mutex;
                std::vector<std::unique_ptr<BundledExecutable>> items;
        };

        public:
            NonPrioWorkerGroup(rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning, int number_of_threads);
            virtual ~NonPrioWorkerGroup();

        protected:
            std::unique_ptr<BundledExecutable> take_next_ready_executable() override;
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) override;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) override;
            void update_prio() override;
            void configure_threads() override;
            ReadyVector ready_vector;
    };

    class MutexGroup : public WorkerGroup {
        std::atomic<bool> is_boosted;
        void refresh_priority();

        public:
            MutexGroup(int priority_, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~MutexGroup();
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) override;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) override;
            virtual void update_prio() override;
    };

}
