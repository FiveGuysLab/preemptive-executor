#include "preemptive_executor/preemptive_executor.hpp"

void set_fifo_prio(int priority, std::thread& t){
    const auto param = sched_param{priority};
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
}

namespace preemptive_executor {
    WorkerGroup::ReadyQueue::ReadyQueue(): num_working(0) {}

    WorkerGroup::WorkerGroup(
        int priority_,
        int number_of_threads,
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): priority(priority_), exec_context(context), exec_spinning(spinning), semaphore(0)
    {
        for (int i = 0; i < number_of_threads; i++){
            //spawn number_of_threads amount of threads and populate one worker group per thread
            auto t = std::make_unique<std::thread>([this]() -> void {this->worker_main();});
            set_fifo_prio(this->priority, *t);
            this->threads.push_back(std::move(t));
        }
    }

    WorkerGroup::~WorkerGroup() {
        this->semaphore.release(this->threads.size());

        for (auto& t : this->threads) {
            t->join();
        }
    }

    void WorkerGroup::update_prio() {}

    MutexGroup::MutexGroup(
        int priority_, 
        rclcpp::Context::SharedPtr context,
        const std::atomic_bool& spinning
    ): WorkerGroup(priority_, 1, context, spinning), is_boosted(false) {}

    MutexGroup::~MutexGroup() {}

    void MutexGroup::update_prio() {
        auto& rq = this->ready_queue;
        const int queue_size = static_cast<int>(rq.queue.size_approx());
        const int working = rq.num_working.load();
        const bool should_boost = (queue_size + working) > 1;
        const bool currently_boosted = is_boosted.load();

        if (should_boost == currently_boosted) {
            return;
        }

        if (this->threads.size() != 1) {
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

    void WorkerGroup::worker_main() {

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
            //  try_dequeue atomically removes an element from the queue and moves it into exec
            //  This is equivalent to the original: swap(exec, queue.front()) + queue.pop()
            std::unique_ptr<BundledExecutable> exec = nullptr;
            {
                auto& rq = this->ready_queue;
                // try_dequeue returns false if queue is empty, and doesn't modify exec in that case
                if (!rq.queue.try_dequeue(exec)) {
                    throw std::runtime_error("Ready Q state invalid");
                }
                // If try_dequeue succeeded, exec should be non-null, but check for safety
                if (exec == nullptr) {
                    throw std::runtime_error("Ready Q state invalid");
                }
                rq.num_working++;
            }

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            exec->run();

            {
                auto& rq = this->ready_queue;
                rq.num_working--;
                // Post-run possible unboost
                this->update_prio();
            }
        }
    }
}