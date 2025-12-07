#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <chrono>

/// @brief Represents a message board post
struct Post {
    std::string author;
    std::string title;
    std::string message;
    int clientId = 0;  // Which client posted this (socket ID or client number)
};

/// @brief Represents a server event log entry
struct ServerEvent {
    std::string timestamp;
    std::string event_type;  // "CONNECT", "DISCONNECT", "POST", "GET_BOARD", "ERROR"
    std::string message;     // Human-readable description
    std::string raw_message; // Raw wire format message (optional)
};

/// @brief Shared server state accessible by both server and GUI threads
struct SharedServerState {
    std::vector<Post> messageBoard;
    std::mutex boardMutex;
    
    // Event log (keep last 100 events)
    std::deque<ServerEvent> eventLog;
    std::mutex eventLogMutex;
    
    // Active client tracking
    std::vector<int> activeClientSockets;
    std::mutex clientsMutex;
    
    int activeConnections = 0;
    int totalMessagesReceived = 0;
    int totalMessagesSent = 0;
    int nextClientId = 1;  // Auto-incrementing client ID
    
    bool serverRunning = true;
    
    /// @brief Add an event to the log
    void logEvent(const std::string& event_type, const std::string& message, const std::string& raw_message = "") {
        std::lock_guard<std::mutex> lock(eventLogMutex);
        
        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char timeBuffer[20];
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", localtime(&time));
        
        ServerEvent event{timeBuffer, event_type, message, raw_message};
        eventLog.push_back(event);
        
        // Keep only the last 100 events
        if (eventLog.size() > 100) {
            eventLog.pop_front();
        }
    }
};

/// @brief Global shared state instance
extern SharedServerState g_serverState;

#endif // SHARED_STATE_H
