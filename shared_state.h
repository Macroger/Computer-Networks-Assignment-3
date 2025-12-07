#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <deque>
#include <chrono>

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
};

/// @brief Global shared state instance
extern SharedServerState g_serverState;

struct Message {
    std::string author;
    std::string content;
};

class MessageBoard {
private:
    std::vector<Message> messages;

public:
    // Load messages from file at startup
    void loadFromFile() {
        std::ifstream file(MESSAGEBOARD_FILE);
        if (!file.is_open()) {
            // File doesn't exist yet, start fresh
            return;
        }
        
        std::string line;
        Message currentMessage;
        bool readingAuthor = true;
        
        while (std::getline(file, line)) {
            if (line == "---") {
                // Message separator
                if (!currentMessage.author.empty()) {
                    messages.push_back(currentMessage);
                    currentMessage = Message();
                }
                readingAuthor = true;
            } else if (readingAuthor) {
                currentMessage.author = line;
                readingAuthor = false;
            } else {
                currentMessage.content = line;
            }
        }
        
        // Add last message if exists
        if (!currentMessage.author.empty()) {
            messages.push_back(currentMessage);
        }
        
        file.close();
    }
    
    // Save messages to file at shutdown
    void saveToFile() {
        std::ofstream file(MESSAGEBOARD_FILE);
        if (!file.is_open()) {
            return;
        }
        
        for (const auto& msg : messages) {
            file << msg.author << "\n";
            file << msg.content << "\n";
            file << "---\n";
        }
        
        file.close();
    }
    
    // Add a new message
    void addMessage(const std::string& author, const std::string& content) {
        messages.push_back({author, content});
    }
    
    // Get all messages
    const std::vector<Message>& getMessages() const {
        return messages;
    }
    
    // Clear all messages
    void clear() {
        messages.clear();
    }
};
