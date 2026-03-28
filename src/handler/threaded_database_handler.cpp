#include "threaded_database_handler.hpp"
#include <iostream>

ThreadedDatabaseHandler::ThreadedDatabaseHandler(uint32_t idx) : idx_(idx)
{
    db_thread_ = std::thread(&ThreadedDatabaseHandler::run,this);
}

ThreadedDatabaseHandler::~ThreadedDatabaseHandler()
{
    if (db_thread_.joinable())
    {
        db_thread_.join();
    }
}
void ThreadedDatabaseHandler::run()
{
    while(is_active_)
    {
        if(events_.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        else
        {
            std::lock_guard<std::mutex> guard(mutex_);
            handlingEvent();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            events_.pop_back();
        }
    }
}
global::DatabaseConnection::status ThreadedDatabaseHandler::addEvent(/*param of event*/)
{
    using namespace global::DatabaseConnection;
    std::lock_guard<std::mutex> guard(mutex_);
    auto oldSizeVec = events_.size();
    events_.emplace_back(/*param of event*/);
    return  oldSizeVec!= events_.size() ? status::success : status::error;
}
