#include "preemptive_executor/preemptive_executor.hpp"

void set_fifo_prio(int priority, std::thread& t){
    const auto param = sched_param{priority};
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param); // TODO: We need to test this behaviour
}

namespace preemptive_executor {
    WorkerGroup::ReadyQueue::ReadyQueue(): num_working(0) {}

    WorkerGroup::WorkerGroup(int priority_, int number_of_threads): priority(priority_), semaphore(0) {
        for (int i = 0; i < number_of_threads; i++){
            //spawn number_of_threads amount of threads and populate one worker group per thread
            auto t = std::make_unique<std::thread>([this]() -> void {this->worker_main();}); // TODO: pass in some context, spinning state- maybe in a lambda
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

    MutexGroup::MutexGroup(int priority_): WorkerGroup(priority_, 1), is_boosted(false) {}

    MutexGroup::~MutexGroup() {}

    void MutexGroup::update_prio() {
        // std::lock_guard<std::mutex> guard(this->ready_queue.mutex);

        auto& rq = this->ready_queue;
        const bool should_boost = (rq.queue.size() + rq.num_working) > 1;
        if (should_boost == is_boosted) {
            return;
        }

        if (this->threads.size() != 1) {
            throw std::runtime_error("MutexGroup with multiple threads- invalid state");
        }

        auto& t = *(this->threads.front());
        if (should_boost) {
            set_fifo_prio(MAX_FIFO_PRIO, t);
            is_boosted = true;
            return;
        }
        set_fifo_prio(this->priority, t);
        is_boosted = false;
    }

    void WorkerGroup::worker_main(){

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

            if (!rclcpp::ok()){ // TODO: We didn't pass in the context, so this does nothing
                break;
            }

            // TODO: check exec spinning with a lambda
            // TODO: What is the difference between that at checking if the context is rclcpp::ok ?

            //4: acquire ready queue mutex and 5: pop from ready queue
            std::unique_ptr<BundledExecutable> exec = nullptr;
            {
                auto& rq = this->ready_queue;
                std::lock_guard<std::mutex> guard(rq.mutex);

                if (rq.queue.empty() || rq.queue.front() == nullptr) {
                    throw std::runtime_error("Ready Q state invalid");
                }

                std::swap(exec, rq.queue.front());
                rq.queue.pop();
                rq.num_working++;
            }

            //7: execute executable (placeholder; actual execution integrates with executor run loop)
            exec->run();

            {
                auto& rq = this->ready_queue;
                std::lock_guard<std::mutex> guard(rq.mutex);
                rq.num_working--;
                // Post-run possible unboost
                this->update_prio();
            }
        }
    }
}