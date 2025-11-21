#include "preemptive_executor/preemptive_executor.hpp"

#include <chrono>
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
        int number_of_threads,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): semaphore(0),
       num_working(0),
       priority(priority_),
       exec_context(context),
       exec_spinning(spinning)
    {
        for (int i = 0; i < number_of_threads; i++){
            auto t = std::make_unique<std::thread>([this]() -> void {this->worker_main();});
            configure_thread(*t);
            this->threads.push_back(std::move(t));
        }
    }

    WorkerGroupBase::~WorkerGroupBase() {
        this->semaphore.release(this->threads.size());

        for (auto& t : this->threads) {
            t->join();
        }
    }

    void WorkerGroupBase::configure_thread(std::thread &) {}

    void WorkerGroupBase::update_prio() {}

    size_t WorkerGroupBase::working_count_locked() const {
        std::lock_guard<std::mutex> guard(this->ready_executables_mutex);
        return num_working;
    }

    void WorkerGroupBase::on_executable_complete() {
        {
            std::lock_guard<std::mutex> guard(this->ready_executables_mutex);
            if (this->num_working == 0) {
                throw std::runtime_error("WorkerGroupBase num_working underflow");
            }
            this->num_working--;
        }
        this->after_work_completed();
    }

    WorkerGroup::ReadyQueue::ReadyQueue() {}

    NonPrioWorkerGroup::ReadyVector::ReadyVector() {}

    WorkerGroup::WorkerGroup(
        int priority_,
        int number_of_threads,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): WorkerGroupBase(priority_, number_of_threads, context, spinning) {}

    WorkerGroup::~WorkerGroup() {}

    void WorkerGroup::update_prio() {}

    std::unique_ptr<BundledExecutable> WorkerGroup::take_next_ready_executable() {
        std::scoped_lock lock(this->ready_queue.mutex, this->ready_executables_mutex);
        if (this->ready_queue.queue.empty() || this->ready_queue.queue.front() == nullptr) {
            throw std::runtime_error("Ready queue state invalid");
        }
        auto exec = std::move(this->ready_queue.queue.front());
        this->ready_queue.queue.pop();
        this->num_working++;
        return exec;
    }

    size_t WorkerGroup::push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) {
        size_t pushed = 0;
        {
            std::scoped_lock lock(this->ready_queue.mutex, this->ready_executables_mutex);
            for (auto & bundle : bundles) {
                if (!bundle) {
                    continue;
                }
                this->ready_queue.queue.push(std::move(bundle));
                pushed++;
            }
        }
        bundles.clear();
        if (pushed > 0) {
            this->update_prio();
        }
        return pushed;
    }

    void WorkerGroup::push_ready_executable(std::unique_ptr<BundledExecutable> exec) {
        if (!exec) {
            return;
        }
        {
            std::scoped_lock lock(this->ready_queue.mutex, this->ready_executables_mutex);
            this->ready_queue.queue.push(std::move(exec));
        }
        this->update_prio();
    }

    size_t WorkerGroup::pending_executable_count() const {
        return this->ready_queue.queue.size();
    }

    void WorkerGroup::configure_thread(std::thread & thread) {
        set_fifo_prio(this->priority, thread);
    }

    void WorkerGroup::after_work_completed() {
        this->update_prio();
    }

    NonPrioWorkerGroup::NonPrioWorkerGroup(
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning,
        int number_of_threads
    ): WorkerGroupBase(0, number_of_threads, context, spinning) {}

    NonPrioWorkerGroup::~NonPrioWorkerGroup() {}

    void NonPrioWorkerGroup::configure_thread(std::thread &) {}

    std::unique_ptr<BundledExecutable> NonPrioWorkerGroup::take_next_ready_executable() {
        std::scoped_lock lock(this->ready_vector.mutex, this->ready_executables_mutex);
        if (this->ready_vector.items.empty() || !this->ready_vector.items.back()) {
            throw std::runtime_error("Ready executable state invalid");
        }
        auto exec = std::move(this->ready_vector.items.back());
        this->ready_vector.items.pop_back();
        this->num_working++;
        return exec;
    }

    size_t NonPrioWorkerGroup::push_ready_executables(std::vector<std::unique_ptr<BundledExecutable>>& bundles) {
        size_t pushed = 0;
        {
            std::scoped_lock lock(this->ready_vector.mutex, this->ready_executables_mutex);
            for (auto & bundle : bundles) {
                if (!bundle) {
                    continue;
                }
                this->ready_vector.items.push_back(std::move(bundle));
                pushed++;
            }
        }
        bundles.clear();
        if (pushed > 0) {
            this->update_prio();
        }
        return pushed;
    }

    void NonPrioWorkerGroup::push_ready_executable(std::unique_ptr<BundledExecutable> exec) {
        if (!exec) {
            return;
        }
        {
            std::scoped_lock lock(this->ready_vector.mutex, this->ready_executables_mutex);
            this->ready_vector.items.push_back(std::move(exec));
        }
        this->update_prio();
    }

    size_t NonPrioWorkerGroup::pending_executable_count() const {
        return this->ready_vector.items.size();
    }

    void NonPrioWorkerGroup::update_prio() {}

    void NonPrioWorkerGroup::after_work_completed() {
        this->update_prio();
    }

    MutexGroup::MutexGroup(
        int priority_,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): WorkerGroup(priority_, 1, context, spinning), is_boosted(false) {}

    MutexGroup::~MutexGroup() {}

    void MutexGroup::update_prio() {
        auto& rq = this->ready_queue;

        const bool should_boost = rq.num_pending.load() > 1;
        if (should_boost == is_boosted.load()) {
            return;
        }

        if (this->threads.size() != 1) { // Sanity check
            throw std::runtime_error("MutexGroup with multiple threads- invalid state");
        }

        auto& t = *(this->threads.front());
        if (should_boost) {
            set_fifo_prio(MAX_FIFO_PRIO, t);
            is_boosted.store(true);
            return;
        }
        set_fifo_prio(this->priority, t);
        is_boosted.store(false);
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

            // 4: pop from ready queue (lock-free)
            auto& rq = this->ready_queue;
            std::unique_ptr<BundledExecutable> exec = nullptr;
            // try_dequeue returns false if queue is empty, and doesn't modify exec in that case
            int failed_attempts = 0;
            while (!rq.queue.try_dequeue(exec)) {
                // dequeue failure is has a small chance of being a false negative
                if (failed_attempts++ > 10) {
                    throw std::runtime_error("Ready Q state invalid");
                }
                // brief pause before retrying
                busy_wait_for(spin_period);
            }
            // If try_dequeue succeeded, exec should be non-null, but check for safety
            if (exec == nullptr) {
                throw std::runtime_error("Ready Q state invalid");
            }

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            exec->run();

            rq.num_pending.fetch_sub(1);
            // Post-run possible unboost
            this->update_prio();
        }
    }
}
