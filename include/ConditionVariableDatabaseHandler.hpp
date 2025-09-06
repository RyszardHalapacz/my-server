#pragma once
#include "Idatabasehandler.hpp"
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

    void terminateThreads()override{isActive=false;};
    global::DatabaseConntetion::status addEvent(/*param of event*/)override;
    uint32_t  handlingEvent() override 
    { 
        std::cout <<"  thread id: "<< std::this_thread::get_id() << " number_of task " << nmTask++ << std::endl;
         std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return vecEvent.size();
    };
    
    private:
    std::thread DbThread;
    bool isActive{true};



    std::vector<Event>& vecEvent;           
    std::mutex& mut;                         
    std::condition_variable& condVar;   
    size_t nmTask{0};   
};