#include "preemptive_executor/preemptive_executor.hpp"

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

        auto& t = *(this->threads.front());
        if (should_boost) {
            set_fifo_prio(MAX_FIFO_PRIO, t);
            return;
        }
        set_fifo_prio(this->priority, t);
    }
}