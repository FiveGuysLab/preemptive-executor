#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
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
        protected:
            WorkerGroupBase(int priority_, int number_of_threads, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~WorkerGroupBase();

            virtual void configure_thread(std::thread & thread);
            virtual void update_prio();
            virtual std::unique_ptr<BundledExecutable> take_next_ready_executable() = 0;
            virtual size_t pending_executable_count() const = 0;
            virtual void after_work_completed() = 0;

            size_t working_count_locked() const;
            void on_executable_complete();
            void worker_main();

            mutable std::mutex ready_executables_mutex;
            size_t num_working;
            int priority;
            std::vector<std::unique_ptr<std::thread>> threads;
            rclcpp::Context::SharedPtr exec_context;
            const std::atomic_bool& exec_spinning;

        public:
            std::counting_semaphore<_SEM_MAX_AT_LEAST> semaphore;
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) = 0;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) = 0;
    };

    class WorkerGroup : public WorkerGroupBase {
        class ReadyQueue { // TODO: Lock-free Q needed
            public:
                ReadyQueue();
                std::mutex mutex;
                std::queue<std::unique_ptr<BundledExecutable>> queue;
        };

        protected:
            void configure_thread(std::thread & thread) override;
            std::unique_ptr<BundledExecutable> take_next_ready_executable() override;
            size_t pending_executable_count() const override;

        public:
            WorkerGroup(int priority_, int number_of_threads, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~WorkerGroup();
            virtual void update_prio() override;
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) override;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) override;
            void after_work_completed() override;

        protected:
            ReadyQueue ready_queue;
    };

    class NonPrioWorkerGroup : public WorkerGroupBase {
        class ReadyVector {
            public:
                ReadyVector();
                std::mutex mutex;
                std::vector<std::unique_ptr<BundledExecutable>> items;
        };

        public:
            NonPrioWorkerGroup(rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning, int number_of_threads);
            virtual ~NonPrioWorkerGroup();

        protected:
            void configure_thread(std::thread & thread) override;
            std::unique_ptr<BundledExecutable> take_next_ready_executable() override;
            size_t pending_executable_count() const override;
            virtual size_t push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) override;
            virtual void push_ready_executable(std::unique_ptr<BundledExecutable> bundle) override;
            void update_prio() override;
            void after_work_completed() override;
            ReadyVector ready_vector;
    };

    class MutexGroup : public WorkerGroup {
        std::atomic<bool> is_boosted;
        bool is_boosted;

        public:
            MutexGroup(int priority_, rclcpp::Context::SharedPtr context, const std::atomic_bool& spinning);
            virtual ~MutexGroup();
            virtual void update_prio() override;
    };

}
