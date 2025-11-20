#include "preemptive_executor/bundled_timer.hpp"
#include "rclcpp/logging.hpp"

namespace preemptive_executor
{
    BundledTimer::BundledTimer(rclcpp::TimerBase::SharedPtr timer_, Priv&) : timer(timer_) {}

    std::unique_ptr<BundledExecutable> BundledTimer::take_and_bundle(rclcpp::TimerBase::SharedPtr timer)
    {
        if (!timer->call())
        {
            return nullptr;
        }

        Priv _p;
        return std::make_unique<BundledTimer>(timer, _p);
    }

    void* BundledTimer::get_raw_handle() const
    {
        return timer.get();
    }

    void BundledTimer::run()
    {
        timer->execute_callback();
    }
}