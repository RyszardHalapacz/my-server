#pragma once
#include "database_handler_interface.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>


class ConditionVariableDatabaseHandler : public IDatabaseHandler
{

    public:
       ConditionVariableDatabaseHandler(
        std::vector<Event>& sharedVecEvent,
        std::mutex& sharedMutex,
        std::condition_variable& sharedCondVar);

    ~ConditionVariableDatabaseHandler () override;

    void run()override;

    void terminateThreads()override{is_active_ = false;};
    global::DatabaseConnection::status addEvent(/*param of event*/)override;
    uint32_t  handlingEvent() override
    {
        std::cout <<"  thread id: "<< std::this_thread::get_id() << " number_of task " << num_tasks_++ << std::endl;
         std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return events_.size();
    };

    private:
    std::thread db_thread_;
    bool is_active_{true};

    std::vector<Event>& events_;
    std::mutex& mutex_;
    std::condition_variable& cond_var_;
    size_t num_tasks_{0};
};
