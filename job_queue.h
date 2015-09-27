#ifndef JOB_QUEUE_H_INCLUDED
#define JOB_QUEUE_H_INCLUDED

#include <memory>
#include <functional>

class job_queue {
public:
    using job_type = std::function<void(void)>;
    job_queue();
    ~job_queue();

    void push(job_type j);

    void execute_all();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif