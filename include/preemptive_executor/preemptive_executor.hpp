#ifndef PREEMPTIVE_EXECUTOR
#define PREEMPTIVE_EXECUTOR

#include <chrono>
#include <memory>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <set>
#include <vector>
#include <semaphore>
#include <climits>

namespace preemptive_executor
{
    struct ReadyQueue {
        std::mutex mutex;
        std::queue<rclcpp::AnyExecutable> queue;
    };

    class WorkerGroup {
        public:
            //constructor for WorkerGroup should take in a vector of thread ids and instantiate the semaphore to make those thread id wait on it
            WorkerGroup(): thread_ids(std::vector<std::thread::id>()), semaphore(std::make_shared<std::counting_semaphore<0>>) {}
            ~WorkerGroup();
            std::vector<std::thread::id> thread_ids;
            std::shared_ptr<std::counting_semaphore> semaphore;
            preemptive_executor::ReadyQueue ready_queue;
    };


    struct ThreadGroupAttributes {
        public:
            ThreadGroupAttributes(int tg_id, int number_of_threads, int deadline, int period, int runtime):  tg_id(tg_id), number_of_threads(number_of_threads),  deadline(deadline), period(period), runtime(runtime) {}
            int tg_id;
            int number_of_threads;
            int priority //int from 1-99       
               
    };


    class PreemptiveExecutor : public rclcpp::Executor
    {
    public:
        RCLCPP_SMART_PTR_DEFINITIONS(PreemptiveExecutor)

        //constructor for PreemptiveExecutor
        RCLCPP_PUBLIC explicit PreemptiveExecutor(); //leaving ctor params blank for now

        RCLCPP_PUBLIC virtual ~PreemptiveExecutor();
        RCLCPP_PUBLIC size_t get_number_of_threads() const;

        // // Timeout for getting next executable
        // std::chrono::nanoseconds next_exec_timeout_;

    protected:
        RCLCPP_PUBLIC void spin() override;
        RCLCPP_PUBLIC void run(size_t this_thread_number);
        bool get_next_executable(rclcpp::AnyExecutable &any_executable, std::chrono::nanoseconds timeout = std::chrono::nanoseconds(-1));
        bool get_next_ready_executable( rclcpp::AnyExecutable &any_executable);
        void wait_for_work(std::chrono::nanoseconds timeout);

        //helper methods for preemptive executor
        void spawn_worker_groups(); //called in spin, spawns all WorkerGroups based on thread attributes
        void* get_callback_handle(const rclcpp::AnyExecutable& executable);   //get callback handle from different ROS2 callback types

    private:
        RCLCPP_DISABLE_COPY(PreemptiveExecutor);
        
        //data structures for preemptive executor
        std::unordered_map<int, WorkerGroup> callback_id_worker_group_map; 
        std::unordered_map<int, std::vector<*WorkerGroup> > thread_group_id_worker_map; 

        std::vector<ThreadGroupAttributes> thread_groups;

        //TODO: need a map bw chain id and ready set after ready set is defined 
    };

} 

#endif 
