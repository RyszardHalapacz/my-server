// #include "server.hpp"
// #include <thread>

// Server::Server(uint32_t thread) :
//     maxThreads_(thread)
// {
//     for (uint32_t i = 0; i < maxThreads_; ++i)
//     {
//         vecHadler.emplace_back(std::make_unique<ThreadedDatabaseHandler>());
//     }
// }

// Server::~Server()
// {
//     for(auto & it : vecHadler) it->terminateThreads();
//     vecHadler.clear();
// }

// void Server::run()
// {

//     //httpServ_->run();
// }    

// global::DatabaseConntetion::status Server::addEvent()
// {
//     requestNumber++;
//     using namespace global::DatabaseConntetion;
//     for(int i=0;i<maxThreads_;i++)
//     {
//         if(requestNumber%(i+1)==0) 
//         {
//             return vecHadler[i]->addEvent();
            
//         }
//     }
// }