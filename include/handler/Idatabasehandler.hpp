#pragma once
//#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>

#include <vector>
#include <memory>

#include "event.hpp"
#include "comon.h"
#include "event.hpp"

class IDatabaseHandler 
{
    public:
    IDatabaseHandler(){};   
    virtual ~IDatabaseHandler(){}; 

    IDatabaseHandler(std::string&, uint32_t ){};
    virtual void run() = 0;
    virtual void terminateThreads() = 0;
    virtual global::DatabaseConntetion::status addEvent(/*param of event*/) = 0;
    virtual uint32_t  handlingEvent()  = 0;

};
