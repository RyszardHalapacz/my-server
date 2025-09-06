#pragma once
//#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>


#include "Idatabasehandler.hpp"

class ThreadedDatabaseHandler : public IDatabaseHandler
{
    public:
    ThreadedDatabaseHandler (uint32_t idx);   
    ~ThreadedDatabaseHandler () override; 

    ThreadedDatabaseHandler (std::string&, uint32_t ){};
    void run()override;
    void terminateThreads()override{isActive=false;};
    global::DatabaseConntetion::status addEvent(/*param of event*/)override;
    uint32_t  handlingEvent() override { return vecEvent.size();};
    private:
    std::vector<Event> vecEvent{};
    std::mutex mut;  
    private:
    uint32_t idx_;
    std::thread DbThread;
    bool isActive  {true};

};
