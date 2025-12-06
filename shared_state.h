#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <vector>
#include <string>
#include <mutex>

/// @brief Represents a message board post
struct Post {
    std::string author;
    std::string title;
    std::string message;
};

/// @brief Shared server state accessible by both server and GUI threads
struct SharedServerState {
    std::vector<Post> messageBoard;
    std::mutex boardMutex;
    
    int activeConnections = 0;
    int totalMessagesReceived = 0;
    int totalMessagesSent = 0;
    
    bool serverRunning = true;
};

/// @brief Global shared state instance
extern SharedServerState g_serverState;

#endif // SHARED_STATE_H
