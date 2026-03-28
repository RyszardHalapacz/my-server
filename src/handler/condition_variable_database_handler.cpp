#include "condition_variable_database_handler.hpp"

// ConditionVariableDatabaseHandler

  ConditionVariableDatabaseHandler::ConditionVariableDatabaseHandler(
        std::vector<Event>& sharedVecEvent,
        std::mutex& sharedMutex,
        std::condition_variable& sharedCondVar)
        : events_(sharedVecEvent), mutex_(sharedMutex), cond_var_(sharedCondVar), is_active_(true)
{
    db_thread_ = std::thread(&ConditionVariableDatabaseHandler::run, this);
}

ConditionVariableDatabaseHandler::~ConditionVariableDatabaseHandler()
{

    std::lock_guard<std::mutex> lock(mutex_);
    is_active_ = false;

    cond_var_.notify_all();
    if (db_thread_.joinable())
        db_thread_.join();
}

global::DatabaseConnection::status ConditionVariableDatabaseHandler::addEvent()
{
    using namespace global::DatabaseConnection;
    auto oldSizeVec = events_.size();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.emplace_back(/*param of event*/);
    }
    cond_var_.notify_one();
    return  oldSizeVec!= events_.size() ? status::success : status::error;
}

void ConditionVariableDatabaseHandler::run()
{
    while (is_active_)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this]() { return !events_.empty() || !is_active_; });
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        if (!is_active_ && events_.empty())
            break;
        [[maybe_unused]] Event event = events_.back();
        events_.pop_back();

        lock.unlock();

        handlingEvent();
    }
}
