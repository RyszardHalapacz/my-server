#pragma once
#include <vector>
#include <thread>
#include <future>
#include <vector>



#include "comon.h"
//#include "Idatabasehandler.hpp"
#include "ThreadedDatabaseHandler.hpp"
#include "ConditionVariableDatabaseHandler.hpp"




template<typename HandlerType>
class ServerBase
{
public:
    ServerBase()  = default;
    virtual ~ServerBase()
    {
        for (auto& h : vecHandler)
            h->terminateThreads();
        vecHandler.clear();
    }
     uint32_t getMaxThread () {return maxThreads_;};


     virtual
      //global::DatabaseConntetion::status
       int addEvent(int event) = 0;
      

protected:
    uint32_t maxThreads_ = std::thread::hardware_concurrency();
    uint32_t requestNumber_ {0};
    std::vector<std::unique_ptr<HandlerType>> vecHandler;
    std::mutex mutex_;
};

/// @brief ///////////////////////////
template<>
class ServerBase<void>
{
public:
    ServerBase() = default;
    virtual ~ServerBase() = default;
    virtual int addEvent(int event) = 0;
protected:
    uint32_t requestNumber_ {0};
};


/// @brief //////////////////////////////
class ServerConditionVar : public ServerBase<ConditionVariableDatabaseHandler>
{
public:
    
    ServerConditionVar()
    { 
        for (uint32_t i = 0; i < maxThreads_; ++i)
        {
            vecHandler.emplace_back (std::make_unique<ConditionVariableDatabaseHandler>(vecEvent, mutex_, condVar));
        }
    }

    // global::DatabaseConntetion::status 
    int addEvent(int event) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        vecEvent.emplace_back(/*...event...*/);
        condVar.notify_one();
        return ++event;
       // return global::DatabaseConntetion::status::succes;
    }

    std::vector<Event> vecEvent;
    std::condition_variable condVar;
};


class ServerThreaded : public ServerBase<ThreadedDatabaseHandler>
{
public:
ServerThreaded()
     {
        for (uint32_t i = 0; i < maxThreads_; ++i)
            vecHandler.emplace_back(std::make_unique<ThreadedDatabaseHandler>(i));
    }

    // global::DatabaseConntetion::status
    int addEvent(int event) override
    {
        {

            std::lock_guard<std::mutex> lock(mutex_);
            requestNumber_++;
        }
        uint32_t idx = requestNumber_ % maxThreads_;
         vecHandler[idx]->addEvent();
         return ++event;
    }
};


class ServerAsync : public ServerBase<void> 
{
public:
    ServerAsync() = default;

    int addEvent(int event) override
    {
      
        auto fut = std::async(std::launch::async, [event]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            std::cout << "Handled event " << event << " in thread: " << std::this_thread::get_id() << "\n";
            return event + 1;
        });
       
    futures_.push_back(std::move(fut));

        return event + 1; 
    }

private:
    
    std::vector<std::future<int>> futures_;
};

class ServerSingleThread : public ServerBase<void>
{
public:
    int addEvent(int event) override
    {
       
        std::cout << "Handled event " << event
                  << " in main thread: " << std::this_thread::get_id() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        return event + 1;
    }
};