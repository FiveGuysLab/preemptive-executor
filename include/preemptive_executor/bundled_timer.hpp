#ifndef PREEMPTIVE_EXECUTOR__BUNDLED_TIMER
#define PREEMPTIVE_EXECUTOR__BUNDLED_TIMER

#include "bundled_executable.hpp"
#include <shared_ptr.hpp>
#include "rcl/timer.h"
#include "rclcpp/timer.hpp"

namespace preemptive_executor
{
    class BundledTimer : public BundledExecutable
    {
    protected:
        rclcpp::TimerBase::SharedPtr timer;
        std::shared_ptr<void> data_ptr;
        BundledTimer(rclcpp::TimerBase::SharedPtr timer, std::shared_ptr<void> data_ptr);
    public:
        static std::unique_ptr<BundledExecutable> take_and_bundle(rclcpp::TimerBase::SharedPtr timer, std::shared_ptr<void> data_ptr);
        void run() override;
    };
}

#endif 