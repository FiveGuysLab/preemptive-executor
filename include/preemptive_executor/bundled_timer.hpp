#ifndef PREEMPTIVE_EXECUTOR__BUNDLED_TIMER
#define PREEMPTIVE_EXECUTOR__BUNDLED_TIMER

#include "bundled_executable.hpp"
#include "rcl/timer.h"
#include "rclcpp/timer.hpp"

namespace preemptive_executor
{
    class BundledTimer : public BundledExecutable
    {
    protected:
        rclcpp::TimerBase::SharedPtr timer;

    public:
        BundledTimer(rclcpp::TimerBase::SharedPtr timer, Priv& _);
        static std::unique_ptr<BundledExecutable> take_and_bundle(rclcpp::TimerBase::SharedPtr timer);
        void run() override;
    };
}

#endif 