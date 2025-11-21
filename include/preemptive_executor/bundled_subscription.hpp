#ifndef PREEMPTIVE_EXECUTOR__BUNDLED_SUB
#define PREEMPTIVE_EXECUTOR__BUNDLED_SUB

#include "bundled_executable.hpp"
#include "rclcpp/subscription_base.hpp"

namespace preemptive_executor {
    class BundledSubscription : public BundledExecutable {
      protected:
        rclcpp::SubscriptionBase::SharedPtr subscription;
        rclcpp::MessageInfo message_info;
        BundledSubscription(rclcpp::SubscriptionBase::SharedPtr subscription);

      public:
        virtual void run() = 0;
        virtual void* get_raw_handle() const override;
    };

    class LoanedMsgSubscription : public BundledSubscription {
      protected:
        void* loaned_msg;

      public:
        LoanedMsgSubscription(rclcpp::SubscriptionBase::SharedPtr subscription, Priv& _);
        static std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
        void run() override;
    };

    class GenericMsgSubscription : public BundledSubscription {
      protected:
        std::shared_ptr<void> message;

      public:
        GenericMsgSubscription(rclcpp::SubscriptionBase::SharedPtr subscription, Priv& _);
        static std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
        void run() override;
    };

    class SerializedMsgSubscription : public BundledSubscription {
      protected:
        std::shared_ptr<rclcpp::SerializedMessage> serialized_msg;

      public:
        SerializedMsgSubscription(rclcpp::SubscriptionBase::SharedPtr subscription, Priv& _);
        static std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
        void run() override;
    };

    std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
} // namespace preemptive_executor

#endif