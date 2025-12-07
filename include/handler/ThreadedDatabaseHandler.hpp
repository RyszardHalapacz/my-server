#pragma once
// #include <pqxx/pqxx>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include "logger/Logger.hpp"
#include "Idatabasehandler.hpp"


#include <chrono>
#include <thread>

inline std::uint64_t now_ticks_us() noexcept {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

inline std::uint32_t mapped_thread_id() noexcept {
    return static_cast<std::uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}


class ThreadedDatabaseHandler : public IDatabaseHandler
{
public:
    ThreadedDatabaseHandler(uint32_t idx);
    ~ThreadedDatabaseHandler() override;

    ThreadedDatabaseHandler(std::string &, uint32_t) {};
    void run() override;
    void terminateThreads() override { isActive = false; };
    global::DatabaseConntetion::status addEvent(/*param of event*/) override;
    uint32_t handlingEvent() override
    {
        std::cout <<"test\n";
        logger::Handler::log<MsgTag::Generic>(
            Severity::Info,                               
            now_ticks_us(),                              
            mapped_thread_id(),                           
            /*request_id*/ 0u,                            
            /*class_id*/ static_cast<std::uint16_t>(1u),  
            /*method_id*/ static_cast<std::uint16_t>(1u), 
            /*schema_ver*/ static_cast<std::uint16_t>(1u) 
        );
        return vecEvent.size();
    };

private:
    std::vector<Event> vecEvent{};
    std::mutex mut;

private:
    uint32_t idx_;
    std::thread DbThread;
    bool isActive{true};
};
