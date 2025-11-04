#include "preemptive_executor/bundled_timer.hpp"

namespace preemptive_executor
{
    BundledTimer::BundledTimer(rclcpp::TimerBase::SharedPtr timer_, std::shared_ptr<void> data_ptr_) : timer(timer_), data_ptr(data_ptr_) {}

    std::unique_ptr<BundledExecutable> take_and_bundle(rclcpp::TimerBase::SharedPtr timer, std::shared_ptr<void> data_ptr) {
        auto bundle = std::make_unique<BundledTimer>(timer, data_ptr);

        // notify rcl that we've taken
        timer->call();

        return bundle;
    }

    void BundledTimer::run() {
        timer->execute_callback(data_ptr);
    }
}