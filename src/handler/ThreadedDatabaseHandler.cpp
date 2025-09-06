#include "ThreadedDatabaseHandler.hpp"
#include <iostream>

ThreadedDatabaseHandler::ThreadedDatabaseHandler(uint32_t idx) : idx_(idx)
{
    DbThread = std::thread(&ThreadedDatabaseHandler::run,this);
}

ThreadedDatabaseHandler::~ThreadedDatabaseHandler()
{
    if (DbThread.joinable()) 
    {
        DbThread.join();
    }
}
void ThreadedDatabaseHandler::run()
{
    while(isActive)
    {
           std::cout << "Thread index: " << idx_ << ", thread id: " << std::this_thread::get_id() << std::endl;


        if(vecEvent.empty()) 
        { std::cout << "Thread index: " << idx_ << "is empty" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        else
        {
            std::lock_guard<std::mutex> blockMutex(mut);
            handlingEvent();
            std::cout << "Thread index: " << idx_ << "handlig event" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            vecEvent.pop_back();
        }
    }
}
global::DatabaseConntetion::status ThreadedDatabaseHandler::addEvent(/*param of event*/)
{
    
    using namespace global::DatabaseConntetion;
    std::lock_guard<std::mutex> blockMutex(mut);
    auto oldSizeVec = vecEvent.size();
    vecEvent.emplace_back(/*param of event*/);
    return  oldSizeVec!= vecEvent.size() ? status::succes : status::error;
}