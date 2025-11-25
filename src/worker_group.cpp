#include "preemptive_executor/preemptive_executor.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>

void set_fifo_prio(int priority, std::thread& t){
    const auto param = sched_param{priority};
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
}

void busy_wait_for(std::chrono::nanoseconds duration) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < duration) {
        // busy wait
    }
}

namespace preemptive_executor {
    WorkerGroup::ReadyQueue::ReadyQueue(): num_pending(0) {}

    WorkerGroupBase::WorkerGroupBase(
        int priority_,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): semaphore(0),
       priority(priority_),
       exec_context(context),
       exec_spinning(spinning)
    {}

    WorkerGroupBase::~WorkerGroupBase() {
        this->semaphore.release(this->threads.size());

        for (auto& t : this->threads) {
            t->join();
        }
    }

    void WorkerGroupBase::configure_threads() {}

    void WorkerGroupBase::update_prio() {}

    WorkerGroup::WorkerGroup(
        int priority_,
        int number_of_threads,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): WorkerGroupBase(priority_, context, spinning) {
        for (int i = 0; i < number_of_threads; i++){
            auto t = std::make_unique<std::thread>([this]() -> void {this->worker_main();});
            this->threads.push_back(std::move(t));
        }
    }

    WorkerGroup::~WorkerGroup() {}

    void WorkerGroup::update_prio() {
        const int prev = this->ready_queue.num_pending.fetch_sub(1);
        if (prev <= 0) {
            throw std::runtime_error("Ready queue pending underflow");
        }
    }

    std::unique_ptr<BundledExecutable> WorkerGroup::take_next_ready_executable() {
        std::unique_ptr<BundledExecutable> exec = nullptr;
        const auto spin_period = std::chrono::nanoseconds(_SEM_SPIN_NS);
        int failed_attempts = 0;
        while (!this->ready_queue.queue.try_dequeue(exec)) {
            if (failed_attempts++ > 10) {
                throw std::runtime_error("Ready queue state invalid");
            }
            busy_wait_for(spin_period);
        }
        if (!exec) {
            throw std::runtime_error("Ready queue state invalid");
        }
        return exec;
    }

    size_t WorkerGroup::push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) {
        //removing null/empty bundles
        const auto erase_begin = std::remove_if(
            bundles.begin(), bundles.end(), [](const std::unique_ptr<BundledExecutable>& bundle) { return !bundle; });
        bundles.erase(erase_begin, bundles.end());

        const size_t pushed = bundles.size();
        if (pushed == 0) {
            return 0;
        }

        if (!this->ready_queue.queue.enqueue_bulk(std::make_move_iterator(bundles.begin()), pushed)) {
            throw std::runtime_error("Failed to enqueue ready executables");
        }
        this->ready_queue.num_pending.fetch_add(static_cast<int>(pushed));
        bundles.clear();
        return pushed;
    }

    void WorkerGroup::push_ready_executable(std::unique_ptr<BundledExecutable> exec) {
        if (!exec) {
            return;
        }
        this->ready_queue.queue.enqueue(std::move(exec));
        this->ready_queue.num_pending.fetch_add(1);
    }

    void WorkerGroup::configure_threads() {
        for (auto& t : this->threads) {
            set_fifo_prio(this->priority, *t);
        }
    }

    NonPrioWorkerGroup::NonPrioWorkerGroup(
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning,
        int number_of_threads
    ): WorkerGroupBase(0, context, spinning) {
        for (int i = 0; i < number_of_threads; i++){
            auto t = std::make_unique<std::thread>([this]() -> void {this->worker_main();});
            this->threads.push_back(std::move(t));
        }
    }

    NonPrioWorkerGroup::~NonPrioWorkerGroup() {}

    void NonPrioWorkerGroup::configure_threads() {}

    std::unique_ptr<BundledExecutable> NonPrioWorkerGroup::take_next_ready_executable() {
        std::lock_guard<std::mutex> lock(this->ready_vector.mutex);
        if (this->ready_vector.items.empty() || !this->ready_vector.items.back()) {
            throw std::runtime_error("Ready executable state invalid");
        }
        auto exec = std::move(this->ready_vector.items.back());
        this->ready_vector.items.pop_back();
        return exec;
    }

    size_t NonPrioWorkerGroup::push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) {
        size_t pushed = 0;
        {
            std::lock_guard<std::mutex> lock(this->ready_vector.mutex);
            for (auto & bundle : bundles) {
                if (!bundle) {
                    continue;
                }
                this->ready_vector.items.push_back(std::move(bundle));
                pushed++;
            }
        }
        bundles.clear();
        return pushed;
    }

    void NonPrioWorkerGroup::push_ready_executable(std::unique_ptr<BundledExecutable> exec) {
        if (!exec) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(this->ready_vector.mutex);
            this->ready_vector.items.push_back(std::move(exec));
        }
    }

    void NonPrioWorkerGroup::update_prio() {}

    size_t MutexGroup::push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) {
        const auto pushed = WorkerGroup::push_ready_executables(bundles);
        if (pushed > 0) {
            this->refresh_priority();
        }
        return pushed;
    }

    void MutexGroup::push_ready_executable(std::unique_ptr<BundledExecutable> exec) {
        WorkerGroup::push_ready_executable(std::move(exec));
        this->refresh_priority();
    }

    MutexGroup::MutexGroup(
        int priority_,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): WorkerGroup(priority_, 1, context, spinning), is_boosted(false) {}

    void MutexGroup::refresh_priority() {
        const int pending = this->ready_queue.num_pending.load();
        const bool should_boost = pending > 1;
        if (should_boost == is_boosted.load()) {
            return;
        }

        if (this->threads.size() != 1) { // Sanity check
            throw std::runtime_error("MutexGroup with multiple threads- invalid state");
        }

        auto& t = *(this->threads.front());
        set_fifo_prio(should_boost ? MAX_FIFO_PRIO : this->priority, t);
        is_boosted.store(should_boost);
    }

    MutexGroup::~MutexGroup() {}

    void MutexGroup::update_prio() {
        WorkerGroup::update_prio();
        this->refresh_priority();
    }

    void WorkerGroupBase::worker_main() {

        //1: set timing policy // NOTE: Handled by dispatcher
        //2: register with thread group // NOTE: Registration handled by dispatcher
        const auto spin_period = std::chrono::nanoseconds(_SEM_SPIN_NS);

        while (true) {
            //3: wait on worker group semaphore
            const auto spin_until = std::chrono::steady_clock::now() + spin_period;
            auto acquired = false;

            // busy-wait on semaphore for small time roughly capturing exectuor's working time
            while (!acquired && std::chrono::steady_clock::now() < spin_until) {
                acquired = this->semaphore.try_acquire();
            }

            // If not acquired beyond small time, yield-wait
            if (!acquired) {
                this->semaphore.acquire();
                acquired = true;
            }

            if (!rclcpp::ok(exec_context)|| !exec_spinning.load()){
                break;
            }

            auto exec = this->take_next_ready_executable(); //concurrent queue logic here
            if (!exec) {
                throw std::runtime_error("Ready executable state invalid");
            }

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            exec->run();

            this->update_prio();
        }
    }
}
