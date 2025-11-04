#ifndef PREEMPTIVE_EXECUTOR__BUNDLED_EXEC
#define PREEMPTIVE_EXECUTOR__BUNDLED_EXEC

namespace preemptive_executor
{
    class BundledExecutable
    {
    public:
        virtual void run() = 0;
        virtual ~BundledExecutable() {};
    };
}

#endif 