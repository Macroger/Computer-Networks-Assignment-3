/*
** Filename: server.cpp
** Project: Computer Networks Assignment 3
** Author: Matthew G. Schatz
** Description: This is a TCP server that works as a message board. It listens for incomming connections
**              from clients, receives a request message, and sends back a response to the client containing
**              the requested resource(s).
**              The requirements state that it must handle one connection at a time (a client), with optional points
**              for handling multiple clients simultaneously using a threaded system.
*/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <exception>
#include <iostream>
#include <ctime>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <string_view>
#include <thread>
#include <mutex>
#include "shared_state.h"

using namespace std;

// Instantiate global shared state
SharedServerState g_serverState;

constexpr int INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

/// @brief Delimits the fields in a message.
const string fieldDelimiter = "}+{";

/// @brief Terminates a complete message transmission.
const string transmissionTerminator = "}}&{{";

/// @brief Separates multiple messages in a transmission.
const string messageSeperator = "}#{";

enum class CLIENT_COMMANDS
{
    GET_BOARD,          // A command sent by the client to get the message board
    POST,                 // A command sent by the client to post a new message
    INVALID_COMMAND,    // Sent by client to server if command is not recognized 
    QUIT                // Client indicates it is done and wants to close the connection
};


const std::unordered_map<std::string_view, CLIENT_COMMANDS> kCmdFromStr{
  {"GET_BOARD", CLIENT_COMMANDS::GET_BOARD},
  {"POST",      CLIENT_COMMANDS::POST},
  {"INVALID_COMMAND", CLIENT_COMMANDS::INVALID_COMMAND},
  {"QUIT",      CLIENT_COMMANDS::QUIT},
};

enum class SERVER_RESPONSES
{
    GET_BOARD,          // Server sends to client the message board data
    POST_OK,            // Server sends to client to confirm post was successful
    POST_ERROR,         // Server sends to client to indicate post was unsuccessful
    GET_BOARD_ERROR,    // Server sends to client to indicate get_board was unsuccessful
    INVALID_COMMAND     // Sent by server to client if command is not recognized
};

const std::unordered_map<SERVER_RESPONSES, std::string_view> kCmdToStr{
    {SERVER_RESPONSES::POST_OK,    "POST_OK"},
    {SERVER_RESPONSES::POST_ERROR, "POST_ERROR"},
    {SERVER_RESPONSES::GET_BOARD,  "GET_BOARD"},
    {SERVER_RESPONSES::GET_BOARD_ERROR, "GET_BOARD_ERROR"},
    {SERVER_RESPONSES::INVALID_COMMAND, "INVALID_COMMAND"},
};

/// @brief Represents the result of parsing a client message.
/// Contains either the parsed command and associated data, or an error message.
struct ParseResult {
    bool ok;
    std::string error;           // non-empty on failure
    CLIENT_COMMANDS clientCmd = CLIENT_COMMANDS::INVALID_COMMAND;
    std::vector<Post> posts;     // for POST
    std::string filter_author;   // for GET_BOARD (optional)
    std::string filter_title;    // for GET_BOARD (optional)
};

bool post_handler(const ParseResult& parsed, std::string& errorDetails, int clientId)
{
    // DEBUG: Print what's being posted
    // std::cout << "\n=== POST_HANDLER DEBUG ===" << std::endl;
    // std::cout << "Number of posts to add: " << parsed.posts.size() << std::endl;
    
    // Append each post from the parsed result to the message board
    try{
        // Check if there are posts to add
        if (parsed.posts.empty()) {
            errorDetails = "No posts to add";
            return false; // No posts to add
        }

        // Lock the mutex to ensure thread-safe access to messageBoard
        std::lock_guard<std::mutex> lock(g_serverState.boardMutex);

        for (size_t i = 0; i < parsed.posts.size(); i++)
        {
            Post p = parsed.posts[i];
            // std::cout << "  Adding Post " << i << ": Author=\"" << p.author 
            //           << "\" Title=\"" << p.title 
            //           << "\" Message=\"" << p.message << "\"" << std::endl;
            p.clientId = clientId;  // Tag the post with the client ID
            g_serverState.messageBoard.push_back(p);
            g_serverState.totalMessagesReceived++;
        }
        
        // std::cout << "Total posts in messageBoard after adding: " << g_serverState.messageBoard.size() << std::endl;
        // std::cout << "==========================\n" << std::endl;
    } 
    catch (const std::exception& ex) 
    {
        errorDetails = std::string("Exception occurred while adding posts: ") + ex.what();
        return false; // Error occurred while adding posts
    } 
    catch (...) 
    {
        errorDetails = "Unknown exception occurred while adding posts";
        return false;
    }

    return true;
}

bool quit_handlder(int SocketToClose)
{
    // Perform any necessary cleanup for the client session

    // Immediately close the socket - since the client requested to quit
    close(SocketToClose);
    return true;
}

/// @brief Builds a formatted POST_OK response.
/// @return 
std::string build_post_ok()
{
    // Wire format: "POST_OK}+{}+{}+{}}}&{{"
    string okPost = std::string(kCmdToStr.at(SERVER_RESPONSES::POST_OK));

    std::string author = ""; // No author for OK
    std::string title = "";  // No title for OK
    std::string message = ""; // No message for OK

    // Build and return the complete response string
    string returnResult = 
        okPost + fieldDelimiter +
        author + fieldDelimiter +
        title + fieldDelimiter +
        message + transmissionTerminator;
    
    return returnResult;
}

/// @brief Splits the input text into fields based on the specified delimiter,
///        but only processes up to the specified end position. 
/// @param text The input string to split
/// @param delim The delimiter string used to split fields
/// @param endPos The position in the string up to which to process
/// @return A vector of split fields
static std::vector<std::string> split_fields_until(const std::string& text,const std::string& delim,size_t endPos) 
{
    // A vector to hold the split fields
    std::vector<std::string> out;

    // Current position in the string
    size_t start = 0;

    // Loop to find and extract fields - if we reach endPos, we stop processing
    while (start <= endPos) 
    {
        // Find the next occurrence of the delimiter
        size_t p = text.find(delim, start);

        // If no more delimiters are found or the next delimiter is beyond endPos
        // we take the rest of the string up to endPos
        if (p == std::string::npos || p > endPos) 
        {
            // Extract the last field up to endPos
            out.push_back(text.substr(start, endPos - start));
            break;
        }

        // Extract the field and update the start position
        out.push_back(text.substr(start, p - start));
        start = p + delim.size();
    }

    // Return the collected fields
    return out;
}

/// @brief Handles the GET_BOARD command, returning the message board with optional filters.
/// @param authorFilter 
/// @param titleFilter 
/// @return A string containing the formatted message board data.
std::string get_board_handler(const std::string& authorFilter, const std::string& titleFilter)
{
    // Lock the mutex to ensure thread-safe access to messageBoard
    std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
    
    // DEBUG: Print what's in messageBoard
    // std::cout << "\n=== GET_BOARD_HANDLER DEBUG ===" << std::endl;
    // std::cout << "Total posts in messageBoard: " << g_serverState.messageBoard.size() << std::endl;
    // for (size_t i = 0; i < g_serverState.messageBoard.size(); i++) {
    //     std::cout << "  Post " << i << ": Author=\"" << g_serverState.messageBoard[i].author 
    //               << "\" Title=\"" << g_serverState.messageBoard[i].title 
    //               << "\" Message=\"" << g_serverState.messageBoard[i].message << "\"" << std::endl;
    // }
    // std::cout << "Filters: Author=\"" << authorFilter << "\" Title=\"" << titleFilter << "\"" << std::endl;
    
    // Build a string containing all messages that match optional filters.
    std::string allMessages;
    std::string command = std::string(kCmdToStr.at(SERVER_RESPONSES::GET_BOARD));

    allMessages += command; // Start with command

    // For each message in the message board, append it to allMessages in the correct wire format.
    bool firstPost = true;
    int postsIncluded = 0;
    for (const Post& post : g_serverState.messageBoard)
    {
        // Check if the post matches the filters - if not, skip it.
        if (!authorFilter.empty() && post.author != authorFilter) continue;
        if (!titleFilter.empty() && post.title != titleFilter) continue;
        
        // Add message separator before each post except the first
        if (!firstPost) {
            allMessages += messageSeperator;
        }
        firstPost = false;
        postsIncluded++;
        
        allMessages += fieldDelimiter + post.author + fieldDelimiter + post.title + fieldDelimiter + post.message;
    }

    // std::cout << "Posts included in response: " << postsIncluded << std::endl;
    // std::cout << "Response wire format: " << allMessages << std::endl;
    // std::cout << "================================\n" << std::endl;

    allMessages += transmissionTerminator; // End with terminator

    return allMessages;
}

ParseResult parse_message(const std::string& completeMessage,
                          const std::string& fieldDelimiter,
                          const std::string& messageSeperator,
                          const std::string& transmissionTerminator)
{
    ParseResult res{};
    res.ok = false;

    // DEBUG: Print raw message received
    // std::cout << "\n=== PARSE_MESSAGE DEBUG ===" << std::endl;
    // std::cout << "Raw message received (" << completeMessage.size() << " bytes):" << std::endl;
    // std::cout << "  [" << completeMessage << "]" << std::endl;
    
    if (completeMessage.empty()) {
        res.error = "Empty message received.";
        return res;
    }

    // Find where this logical message ends: the terminator marks the end
    // Note: We include message separators as part of the parsing, not as boundaries
    size_t termPos = completeMessage.find(transmissionTerminator);

    // std::cout << "Terminator pos: " << (termPos == std::string::npos ? -1 : (int)termPos) << std::endl;

    // Determine the end position: use terminator if found, otherwise full message length
    size_t endPos = completeMessage.size();
    if (termPos != std::string::npos) endPos = termPos;

    // std::cout << "Using endPos: " << endPos << std::endl;
    // std::cout << "Message chunk to parse: [" << completeMessage.substr(0, endPos) << "]" << std::endl;

    // Tokenize fields up to endPos, treating message separators as regular field delimiters
    // First, temporarily replace message separators with field delimiters for parsing
    std::string messageToTokenize = completeMessage.substr(0, endPos);
    size_t replacePos = 0;
    while ((replacePos = messageToTokenize.find(messageSeperator, replacePos)) != std::string::npos) {
        messageToTokenize.replace(replacePos, messageSeperator.length(), fieldDelimiter);
        replacePos += fieldDelimiter.length();
    }
    
    auto fields = split_fields_until(messageToTokenize, fieldDelimiter, messageToTokenize.size());
    if (fields.empty()) {
        res.error = "Malformed message: no fields found.";
        return res;
    }

    // 1) Command (first field)
    const std::string& commandStr = fields[0];
   
    // Map command string to enum - determines which command was sent
    // Create an iterator to search the map - look for the command string
    auto it = kCmdFromStr.find(commandStr);

    // If the iterator is not at the end, the command was found
    bool found = (it != kCmdFromStr.end());

    // Set the command in the result based on whether it was found
    if (found) 
    {
        res.clientCmd = it->second;  // map value (the enum)
    } 
    else 
    {
        res.clientCmd = CLIENT_COMMANDS::INVALID_COMMAND;
        res.error = "Invalid command: " + commandStr;
        return res;
    }

    // 2) Payload depends on command
    if (res.clientCmd == CLIENT_COMMANDS::GET_BOARD) 
    {
        // Optional filters: Author, Title
        if (fields.size() > 1) res.filter_author = fields[1];
        if (fields.size() > 2) res.filter_title  = fields[2];
        res.ok = true;
        return res;
    }

    if (res.clientCmd == CLIENT_COMMANDS::POST) 
    {
        const size_t payloadCount = fields.size() - 1;
        
        // Check if the payload count is valid (must be non-zero and a multiple of 3)
        // Even if the segments are emtpy, they should still be there
        if (payloadCount == 0) 
        {
            res.error = "POST contains no (Author, Title, Message) sets.";
            return res;
        }

        // Basically the same as above, but checking for triples
        // Essentially all the segments should be there, even if some are empty
        if (payloadCount % 3 != 0) 
        {
            res.error = "POST requires triples of Author, Title, Message.";
            return res;
        }

        // Process each set of (Author, Title, Message) 
        // Loop through the fields and build Post objects to be placed in the result
        for (size_t i = 1; i + 2 < fields.size(); i += 3) 
        {
            const std::string& author  = fields[i];     // may be empty (anonymous)
            const std::string& title   = fields[i+1];   // may be empty
            const std::string& message = fields[i+2];   // should exist

            // Enforce non-empty message
            if (message.empty()) 
            { 
                res.error = "POST message cannot be empty."; 
                return res;
            }

            // Create the Post object and populate it with data
            Post p{author, title, message};

            // Add the post to the posts vector in the result
            res.posts.push_back(std::move(p));
        }

        // Loop complete, all posts processed successfully
        res.ok = true;
        // std::cout << "Parsed " << res.posts.size() << " POST messages" << std::endl;
        // std::cout << "===========================\n" << std::endl;
        return res;
    }

    if (res.clientCmd == CLIENT_COMMANDS::QUIT) 
    {
        res.ok = true;
        // std::cout << "Parsed QUIT command" << std::endl;
        // std::cout << "===========================\n" << std::endl;
        return res;
    }

    // If we reach here, something went wrong
    res.error = "Unhandled command or parsing error.";
    // std::cout << "===========================\n" << std::endl;
    return res;
}

/// @brief Sends all bytes in the buffer over the specified socket.
/// @param socket The socket to send data through.
/// @param buffer Pointer to the data buffer to send.
/// @param length The total number of bytes to send.
/// @param flags Flags to modify the behavior of the send operation.
/// @return The total number of bytes sent on success, or -1 on error.
ssize_t send_all_bytes(int socket, const char* buffer, size_t length, int flags)
{
    size_t totalSent = 0;
    while (totalSent < length)
    {
        // Perform the send and record the bytes sent
        ssize_t bytesSent = send(socket, buffer + totalSent, length - totalSent, flags);
        
        // If bytes were sent successfully, update the total and continue
        if(bytesSent > 0)
        {
            totalSent += bytesSent; // Update total bytes sent
            continue;   // Continue sending remaining bytes
        }
        
        // Check if the send was interrupted by a signal issue - if so, retry sending
        if(bytesSent && errno == EINTR)
        {
            continue; // Interrupted by signal, retry sending
        }
        
        // An error occurred during sending
        std::cerr << "Error sending data: " << strerror(errno) << std::endl;
        return -1;
    }
    return totalSent;
}

/// @brief Reads data from a socket until a specified terminator string is found.
/// @param socket The socket to read data from.
/// @param messageBuffer A buffer that accumulates incoming data.
/// @param terminator The string that indicates the end of a complete message.
/// @param completedMessage A string to store the complete message once the terminator is found.
/// @return True if a complete message was successfully read; false otherwise.
bool read_message_until_terminator(
    int socket,
    std::string& messageBuffer,
    const std::string& terminator,
    std::string &completedMessage
)
{
    // Quick check: maybe messageBuffer already contains a full message
    auto pos = messageBuffer.find(terminator);
    if (pos != std::string::npos)
    {
        // Extract message up to the terminator and remove processed bytes
        completedMessage = messageBuffer.substr(0, pos);
        messageBuffer.erase(0, pos + terminator.size());
        return true;
    }

    char temp[4096] = {}; // Temporary buffer for receiving data
    while(true)
    {
        ssize_t bytesReceived = recv(socket, temp, sizeof(temp), 0);
        if (bytesReceived > 0)
        {
            // Append received data to the message buffer
            messageBuffer.append(temp, bytesReceived);

            // Check if the terminator is now present in the buffer
            auto pos = messageBuffer.find(terminator);
            if (pos != std::string::npos)
            {
                // Extract message up to the terminator and remove processed bytes
                completedMessage = messageBuffer.substr(0, pos);
                messageBuffer.erase(0, pos + terminator.size());
                return true;
            }

            // No terminator found yet, continue receiving
            continue;
        }

        // Check if we received zero bytes - if this occurs before
        // the terminator is found, the connection has been closed by the client
        if (bytesReceived == 0)
        {
            // Connection has been closed by the peer
            std::cerr << "Connection closed by peer." << std::endl;
            return false;
        }

        // An error occurred during receiving
        if (errno == EINTR)
        {
            continue; // Interrupted by signal, retry receiving
        }

        std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
        return false;
    }
}

/// @brief Builds a formatted POST_ERROR response.
/// @param errorMessage The error description to include.
/// @return A wire-format error response ready to send to client.
std::string handle_post_error(const std::string& errorMessage) 
{
    // Wire format: "POST_ERROR}+{error_message}}&{{"
    string errorPost = std::string(kCmdToStr.at(SERVER_RESPONSES::POST_ERROR));

    string emptyAuthor = ""; // No author for error
    string emptyTitle = "";  // No title for error

    // Build and return the complete response string
    string returnResult = 
        errorPost + fieldDelimiter +
        emptyAuthor + fieldDelimiter +
        emptyTitle + fieldDelimiter +
        errorMessage + transmissionTerminator;
    return returnResult;
}
    



/// @brief Processes a parsed client message and generates a server response.
/// @param parsed The ParseResult from parse_message() containing the command and payload.
/// @param CommunicationSocket The socket for this client connection
/// @param clientId The unique ID assigned to this client
/// @return A string response to send back to the client (wire format).
void handle_client_request(const ParseResult& parsed, int CommunicationSocket, int clientId)
{
    // Error checking: if parsing failed, send an error response
    if (!parsed.ok) 
    {
        // Send back an error response
        g_serverState.logEvent("ERROR", "Invalid command: " + parsed.error);
        std::string emptyAuthor = "";
        std::string emptyTitle = "";
        std::string response = "INVALID_COMMAND" + fieldDelimiter + emptyAuthor + fieldDelimiter + emptyTitle + fieldDelimiter + parsed.error + transmissionTerminator;
        send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
        return;
    }

    // Dispatch based on command type
    switch (parsed.clientCmd) {

        case CLIENT_COMMANDS::GET_BOARD: 
        {
            // Call get_board_handler with optional filters - generate a message containing the whole board.
            std::string raw_msg = "GET_BOARD}+{" + parsed.filter_author + "}+{" + parsed.filter_title + "}}&{{";
            g_serverState.logEvent("GET_BOARD", "Client requested board (socket: " + std::to_string(CommunicationSocket) + ")", raw_msg);
            std::string response = get_board_handler(parsed.filter_author, parsed.filter_title);
            send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            return;     
        }

        case CLIENT_COMMANDS::POST: 
        {
            std::string errorMessage;
            bool result = post_handler(parsed, errorMessage, clientId);
            if(result == false)
            {
                // Build and send POST_ERROR response
                g_serverState.logEvent("POST_ERROR", errorMessage);
                std::string response = handle_post_error(errorMessage);
                send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            }
            else
            {
                // Build and send POST_OK response
                // Reconstruct raw message from parsed posts
                std::string raw_msg = "POST";
                for (const auto& post : parsed.posts) {
                    raw_msg += "}+{" + post.author + "}+{" + post.title + "}+{" + post.message;
                    if (&post != &parsed.posts.back()) raw_msg += "}#{";
                }
                raw_msg += "}}&{{";
                g_serverState.logEvent("POST", "Client posted " + std::to_string(parsed.posts.size()) + " message(s) (socket: " + std::to_string(CommunicationSocket) + ")", raw_msg);
                std::string response = build_post_ok();
                send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            }
            return;
        }

        case CLIENT_COMMANDS::QUIT:
            // Handled in main loop before calling this function
            return;

        case CLIENT_COMMANDS::INVALID_COMMAND:
        default: {
            std::string emptyAuthor = "";
            std::string emptyTitle = "";
            std::string message = "Error, unable to interpret command - make sure to use accepted legitimate commands!";

            std::string response = "INVALID_COMMAND" + fieldDelimiter + emptyAuthor + fieldDelimiter + emptyTitle + fieldDelimiter + message + transmissionTerminator;
            send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            return;
        }
    }
}

/// @brief Handles communication with a single client in its own thread
/// @param CommunicationSocket The socket for this client connection
void client_handler(int CommunicationSocket)
{
    std::string RxBuffer;        // A buffer to hold received data
    std::string CompletedMessage; // A complete message once terminator is found
    bool keepRunning = true;

    // Assign unique client ID
    int myClientId;
    {
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        myClientId = g_serverState.nextClientId++;
        g_serverState.activeClientSockets.push_back(CommunicationSocket);
    }

    // Increment active connections
    g_serverState.activeConnections++;
    g_serverState.logEvent("CONNECT", "Client #" + std::to_string(myClientId) + " connected (socket: " + std::to_string(CommunicationSocket) + ")");

    while (keepRunning) {

        // Read a complete message
        bool result = read_message_until_terminator(
            CommunicationSocket,
            RxBuffer,
            transmissionTerminator,
            CompletedMessage
        );

        // Connection closed or error - exit loop
        if (!result) {
            g_serverState.logEvent("DISCONNECT", "Client disconnected (socket: " + std::to_string(CommunicationSocket) + ")");
            keepRunning = false;
            break;
        }

        // std::cout << "Received message from client: " << CompletedMessage << std::endl;

        // Parse the complete message (which may contain multiple POST triples separated by }#{)
        ParseResult parsed = parse_message(
            CompletedMessage, 
            fieldDelimiter, 
            messageSeperator, 
            transmissionTerminator
        );

        // Check if client wants to quit
        if (parsed.ok && parsed.clientCmd == CLIENT_COMMANDS::QUIT) {
            g_serverState.logEvent("QUIT", "Client requested disconnect (socket: " + std::to_string(CommunicationSocket) + ")");
            
            // Send goodbye message
            std::string emptyAuthor = "SERVER";
            std::string emptyTitle = "BYE!!!";
            std::string message = "Server says: BYE!!!";
            std::string response = "QUIT" + fieldDelimiter + emptyAuthor + fieldDelimiter + emptyTitle + fieldDelimiter + message + transmissionTerminator;
            send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            
            keepRunning = false;
            break;
        }

        // Handle the request and send response
        handle_client_request(parsed, CommunicationSocket, myClientId);

        // Clear the completed message for next iteration
        CompletedMessage.clear();
    }

    // Cleanup and close the socket for this client
    close(CommunicationSocket);
    
    // Remove from active clients list
    {
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        auto it = std::find(g_serverState.activeClientSockets.begin(), 
                           g_serverState.activeClientSockets.end(), 
                           CommunicationSocket);
        if (it != g_serverState.activeClientSockets.end()) {
            g_serverState.activeClientSockets.erase(it);
        }
    }
    
    // Decrement active connections
    g_serverState.activeConnections--;
}

/// @brief Server main loop - accepts connections and spawns client handler threads
/// Uses g_serverState.serverRunning to determine when to shutdown
void server_run_loop()
{
    //constexpr const char* SERVER_ADDR = "0.0.0.0"; // Listen on all interfaces
    constexpr int SERVER_PORT = 26500;

    int ListeningSocket;          // The socket used to listen for incoming connections
    int CommunicationSocket;      // The socket used for communication with the client

    struct sockaddr_in SvrAddr;  // Server address structure

    // Setup the server socket for TCP
    ListeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListeningSocket == INVALID_SOCKET)
    {
        g_serverState.logEvent("ERROR", "Socket creation failed: " + std::string(strerror(errno)));
        close(ListeningSocket);
        return;
    }

    // Set SO_REUSEADDR to allow immediate reuse of the port after server restart
    int reuse = 1;
    if (setsockopt(ListeningSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        g_serverState.logEvent("WARNING", "Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
    }

    // Configure binding settings and bind the socket
    SvrAddr.sin_family = AF_INET;               // Use the Internet address family
    SvrAddr.sin_addr.s_addr = INADDR_ANY;       // Accept connections from any address
    SvrAddr.sin_port = htons(SERVER_PORT);      // Set port to 26500
    if (bind(ListeningSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr)) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        g_serverState.logEvent("ERROR", "Failed to bind ServerSocket: " + std::string(strerror(errno)));
        return;
    }

    // Start listening on the configured socket
    if (listen(ListeningSocket, 1) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        g_serverState.logEvent("ERROR", "Failed to configure listen on ServerSocket: " + std::string(strerror(errno)));
        return;
    }

    g_serverState.logEvent("SERVER", "Server is listening for connections on port 26500...");

    // Accept multiple connections and spawn a thread for each client
    std::vector<std::thread> clientThreads;
    
    while (g_serverState.serverRunning) {
        // Accept a connection on the socket - spin up a new socket for communication
        CommunicationSocket = accept(ListeningSocket, NULL, NULL);
        if (CommunicationSocket == SOCKET_ERROR)
        {
            g_serverState.logEvent("WARNING", "Failed to accept connection on ServerSocket: " + std::string(strerror(errno)));
            continue;  // Keep trying to accept more connections
        }
        
        // Spawn a new thread to handle this client
        // Use detach() since we don't need to wait for client threads to finish
        std::thread t(client_handler, CommunicationSocket);
        t.detach();
    }

    // Cleanup and close the listening socket
    close(ListeningSocket);
    std::cout << "Server shutdown successfully." << std::endl;
}

#if !defined(UNIT_TEST) && !defined(GUI_BUILD)
int main()
{
    server_run_loop();
    return 0;
}
#endif
