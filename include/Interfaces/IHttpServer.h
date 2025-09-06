#ifndef I_HTTP_SERVER_H
#define I_HTTP_SERVER_H

#include <string>
#include <functional>
#include <map>
#include "comon.h"

// Alias for a request handler function
using HttpRequestHandler = std::function<std::string(const std::string&, const std::map<std::string, std::string>&)>;

class IHttpServer {
public:
    // Rule of Five - default implementations
    virtual ~IHttpServer() = default;                      // Virtual destructor
    IHttpServer() = default;                               // Default constructor
    IHttpServer(const IHttpServer&) = default;              // Copy constructor
    IHttpServer& operator=(const IHttpServer&) = default;   // Copy assignment
    IHttpServer(IHttpServer&&) = default;                   // Move constructor
    IHttpServer& operator=(IHttpServer&&) = default;        // Move assignment

    // Interface methods
    virtual void setPort(int port) = 0;
    virtual void addRoute(const std::string& path, HttpRequestHandler handler) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

#endif // I_HTTP_SERVER_H
