# 📦 Server – Performance and Request Handling Architecture Testing

This project aims to test various approaches to organizing and optimizing server performance in a multithreaded environment. Currently, it focuses on handling requests through dedicated threads and synchronizing data access.

## 🎯 Project Goal  
The main objective is to compare alternative strategies for processing data within the server — both in terms of code organization and potential performance. This serves as a foundation for future development of a lightweight, scalable, and maintainable system.

## 🧵 Currently Implemented Approaches

### Static assignment of handlers to threads

- The server creates a fixed number of `Handler` class instances, each assigned to a specific thread.  
- Requests are distributed among available handlers sequentially (e.g., round-robin).  
- No dynamic synchronization — responsibility is statically assigned.

### Handling with Condition Variables

- The server adds events to a shared queue and notifies one waiting thread using `notify_one()`.  
- Threads (handlers) act more reactively — they wait for signals and process events when available.  
- This model is more dynamic and flexible but requires synchronized resource access.

## 🔍 What Currently Works

- Event registration and handling.  
- Event processing across multiple threads.  
- Dynamic addition of events to the queue.  
- Simple comparative tests of the two models’ behavior.
