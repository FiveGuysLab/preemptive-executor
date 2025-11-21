#ifndef PREEMPTIVE_EXECUTOR__BUNDLED_EXEC
#define PREEMPTIVE_EXECUTOR__BUNDLED_EXEC

namespace preemptive_executor {
    class BundledExecutable {
      protected:
        struct Priv {};

      public:
        virtual void run() = 0;
        virtual void* get_raw_handle() const = 0;
        virtual ~BundledExecutable() {};
    };
} // namespace preemptive_executor

#endif