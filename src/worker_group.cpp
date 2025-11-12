#include "preemptive_executor/preemptive_executor.hpp"

namespace preemptive_executor {
    WorkerGroup::ReadyQueue::ReadyQueue(): num_working(0) {}

    WorkerGroup::WorkerGroup(int priority_): priority(priority_), semaphore(std::make_unique<std::counting_semaphore<_CORE_COUNT>>(0)) {}

    WorkerGroup::~WorkerGroup() {
        this->semaphore->release(this->threads.size());

        for (auto& t : this->threads) {
            t->join();
        }
    }

    void WorkerGroup::update_prio() {}

    MutexGroup::MutexGroup(int priority_): WorkerGroup(priority_), is_boosted(false) {}

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