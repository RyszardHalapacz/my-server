#include "ConditionVariableDatabaseHandler.hpp"

// ConditionVariableDatabaseHandler

  ConditionVariableDatabaseHandler::ConditionVariableDatabaseHandler(
        std::vector<Event>& sharedVecEvent,
        std::mutex& sharedMutex,
        std::condition_variable& sharedCondVar)
        : vecEvent(sharedVecEvent), mut(sharedMutex), condVar(sharedCondVar), isActive(true)
{
    DbThread = std::thread(&ConditionVariableDatabaseHandler::run, this);
}

ConditionVariableDatabaseHandler::~ConditionVariableDatabaseHandler()
{

    std::lock_guard<std::mutex> lock(mut);
    isActive = false;

    condVar.notify_all();
    if (DbThread.joinable())
        DbThread.join();
}

global::DatabaseConntetion::status ConditionVariableDatabaseHandler::addEvent()
{
    using namespace global::DatabaseConntetion;
    auto oldSizeVec = vecEvent.size();
    {
        std::lock_guard<std::mutex> lock(mut);
        vecEvent.emplace_back(/*param of event*/);
    }
    condVar.notify_one();
    return  oldSizeVec!= vecEvent.size() ? status::succes : status::error;
}

void ConditionVariableDatabaseHandler::run()
 {
        while (isActive)
        {
            std::unique_lock<std::mutex> lock(mut);
            condVar.wait(lock, [this]() { return !vecEvent.empty() || !isActive; });
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            if (!isActive && vecEvent.empty())
                break;
          [[maybe_unused]]  Event event = vecEvent.back();
            vecEvent.pop_back();

            lock.unlock();

            handlingEvent();
        }
    }

