#ifndef PREEMPTIVE_EXECUTOR__BUNDLED_SUB
#define PREEMPTIVE_EXECUTOR__BUNDLED_SUB

#include "bundled_executable.hpp"
#include <shared_ptr.hpp>
#include "rclcpp/subscription_base.hpp"

namespace preemptive_executor
{
	class BundledSubscription : public BundledExecutable
	{
	protected:
		rclcpp::SubscriptionBase::SharedPtr subscription;
		rclcpp::MessageInfo message_info;

	public:
		BundledSubscription(rclcpp::SubscriptionBase::SharedPtr subscription);
		virtual void run() = 0;
	};

	class LoanedMsgSubscription : public BundledSubscription
	{
		void *loaned_msg;
		LoanedMsgSubscription(rclcpp::SubscriptionBase::SharedPtr subscription);

	public:
		static std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
		void run() override;
	};

	class GenericMsgSubscription : public BundledSubscription
	{
		std::shared_ptr<void> message;
		GenericMsgSubscription(rclcpp::SubscriptionBase::SharedPtr subscription);

	public:
		static std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
		void run() override;
	};

	class SerializedMsgSubscription : public BundledSubscription
	{
		std::shared_ptr<rclcpp::SerializedMessage> serialized_msg;
		SerializedMsgSubscription(rclcpp::SubscriptionBase::SharedPtr subscription);

	public:
		static std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
		void run() override;
	};

	std::unique_ptr<BundledSubscription> take_and_bundle(rclcpp::SubscriptionBase::SharedPtr subscription);
}

#endif