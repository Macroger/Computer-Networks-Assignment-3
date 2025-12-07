#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <deque>
#include <chrono>
#include <iostream>

const std::string MESSAGEBOARD_FILE = "MessageBoard.txt";

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
    
    /// @brief Load message board from file at startup
    void loadFromFile() {
        std::lock_guard<std::mutex> lock(boardMutex);
        
        std::ifstream file(MESSAGEBOARD_FILE);
        if (!file.is_open()) {
            // File doesn't exist yet, start fresh
            logEvent("SYSTEM", "No saved messages found, starting with empty board");
            return;
        }
        
        messageBoard.clear();
        std::string line;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            // Expected format per post:
            // AUTHOR|TITLE|MESSAGE|CLIENTID
            std::istringstream iss(line);
            std::string author, title, message, clientIdStr;
            
            if (std::getline(iss, author, '|') &&
                std::getline(iss, title, '|') &&
                std::getline(iss, message, '|') &&
                std::getline(iss, clientIdStr, '|')) {
                
                Post p;
                p.author = author;
                p.title = title;
                p.message = message;
                p.clientId = std::stoi(clientIdStr);
                
                messageBoard.push_back(p);
            }
        }
        
        file.close();
        logEvent("SYSTEM", "Loaded " + std::to_string(messageBoard.size()) + " messages from file");
    }
    
    /// @brief Save message board to file
    void saveToFile() {
        std::lock_guard<std::mutex> lock(boardMutex);
        
        std::ofstream file(MESSAGEBOARD_FILE);
        if (!file.is_open()) {
            logEvent("ERROR", "Failed to save messages to file");
            return;
        }
        
        for (const auto& post : messageBoard) {
            // Format: AUTHOR|TITLE|MESSAGE|CLIENTID
            file << post.author << "|"
                 << post.title << "|"
                 << post.message << "|"
                 << post.clientId << "\n";
        }
        
        file.close();
        logEvent("SYSTEM", "Saved " + std::to_string(messageBoard.size()) + " messages to file");
    }
};

/// @brief Global shared state instance
extern SharedServerState g_serverState;