#include "preemptive_executor/bundled_timer.hpp"
#include "rclcpp/logging.hpp"

namespace preemptive_executor
{
    BundledTimer::BundledTimer(rclcpp::TimerBase::SharedPtr timer_, std::shared_ptr<void> data_ptr_) : timer(timer_), data_ptr(data_ptr_) {}

    std::unique_ptr<BundledExecutable> take_and_bundle(rclcpp::TimerBase::SharedPtr timer)
    {
        std::shared_ptr<void> data_ptr = timer->call();

        if (data_ptr == nullptr)
        {
            return nullptr;
        }

        return std::make_unique<BundledTimer>(timer, data_ptr);
    }

    void BundledTimer::run()
    {
        if (data_ptr == nullptr)
        {
            RCLCPP_ERROR(
                rclcpp::get_logger("rclcpp"),
                "executor %s '%s' unexpectedly failed: %s",
                "BundledTimer run called twice",
                timer->get_timer_handle().get(),
                std::runtime_error("BundledTimer run called twice"));
        }

        timer->execute_callback(data_ptr);

        data_ptr = nullptr;
    }
}