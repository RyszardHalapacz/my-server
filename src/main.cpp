#include "server.hpp"
#include "event.hpp"

int main()
{
    ServerThreaded  serv;
    //ServerConditionVar serv;
    //ServerAsync serv;
    //ServerSingleThread serv;
    // serv.run();
    int request = 0;
    Event event;
    while( request <2)
    {
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          serv.addEvent(event);
          request++;
          
    }
    return 0;
}


