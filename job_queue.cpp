#include "job_queue.h"
#include <mutex>
#include <queue>

class job_queue::impl {
public:
    impl() {
    }
    ~impl() {
    }

    void push(job_type j) {
        std::lock_guard<std::mutex> lock_(mutex_);
        queue_.push(j);
    }
    
    void execute_all() {
        for (;;) {
            job_type j;
            {
                std::lock_guard<std::mutex> lock_(mutex_);
                if (queue_.empty()) {
                    return;
                }
                j = queue_.front();
                queue_.pop();
            }
            j();
        }
    }

private:
    std::mutex               mutex_;
    std::queue<job_type>     queue_;
};

job_queue::job_queue() : impl_(new impl{})
{
}

job_queue::~job_queue() = default;

void job_queue::push(job_type j)
{
    impl_->push(j);
}

void job_queue::execute_all()
{
    return impl_->execute_all();
}