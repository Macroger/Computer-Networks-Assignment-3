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
#include <iostream>
#include <ctime>
#include <vector>
#include <unordered_map>
#include <string_view>

using namespace std;

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
    //DELETE_POST,        // A command sent by the client to request deletion of a post - NOT IMPLEMENTED
    //UPDATE_POST,        // A command sent by the client to request an update to a post - NOT IMPLEMENTED
    QUIT                // Client indicates it is done and wants to close the connection
};

const std::unordered_map<std::string_view, CLIENT_COMMANDS> kCmdFromStr{
  {"GET_BOARD", CLIENT_COMMANDS::GET_BOARD},
  {"POST",      CLIENT_COMMANDS::POST},
  {"QUIT",      CLIENT_COMMANDS::QUIT},
};

enum class SERVER_RESPONSES
{
    POST_OK,            // Server sends to client to confirm post was successful
    POST_ERROR,         // Server sends to client to indicate post was unsuccessful
    INVALID_COMMAND    // Sent by server to client if command is not recognized
};

const std::unordered_map<SERVER_RESPONSES, std::string_view> kCmdToStr{
  {SERVER_RESPONSES::POST_OK,    "POST_OK"},
  {SERVER_RESPONSES::POST_ERROR, "POST_ERROR"},
  {SERVER_RESPONSES::INVALID_COMMAND, "INVALID_COMMAND"},
};

/// @brief Represents a message board post.
struct Post {
    //int id;                // assigned by server storage
    std::string author;
    std::string title;
    std::string message;
    //std::time_t ts;     // timestamp - optional feature, not implemented
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
    return out;
}

ParseResult parse_message(const std::string& completeMessage,
                          const std::string& fieldDelimiter,
                          const std::string& messageSeperator,
                          const std::string& transmissionTerminator)
{
    ParseResult res{};
    res.ok = false;

    if (completeMessage.empty()) {
        res.error = "Empty message received.";
        return res;
    }

    // Find where this logical message ends: first separator or terminator.
    size_t sepPos  = completeMessage.find(messageSeperator);
    size_t termPos = completeMessage.find(transmissionTerminator);

    // Determine the earliest of separator or terminator, or the full message length if neither found
    size_t endPos = completeMessage.size();
    if (sepPos != std::string::npos)  endPos = std::min(endPos, sepPos);
    if (termPos != std::string::npos) endPos = std::min(endPos, termPos);

    // Tokenize fields up to endPos
    auto fields = split_fields_until(completeMessage, fieldDelimiter, endPos);
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
        return res;
    }

    if (res.clientCmd == CLIENT_COMMANDS::QUIT) 
    {
        res.ok = true;
        return res;
    }

    // If we reach here, something went wrong
    res.error = "Unhandled command or parsing error.";
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

/// @brief Processes a parsed client message and generates a server response.
/// @param parsed The ParseResult from parse_message() containing the command and payload.
/// @return A string response to send back to the client (wire format).
std::string handle_client_request(const ParseResult& parsed)
{
    // Error checking: if parsing failed, return error response
    if (!parsed.ok) {
        // Send back an error response (you may want to define the wire format)
        string returnResult = "ERROR: " + parsed.error + " " + std::string(kCmdToStr.at(SERVER_RESPONSES::INVALID_COMMAND));
        return returnResult;
    }

    // Dispatch based on command type
    switch (parsed.clientCmd) {
        
        case CLIENT_COMMANDS::GET_BOARD:
            // TODO: Call get_board_handler(parsed.filter_author, parsed.filter_title);
            return "GET_BOARD handler not yet implemented.";
        
        case CLIENT_COMMANDS::POST:
            // TODO: Call post_handler(parsed.posts);
            return "POST handler not yet implemented.";
        
        case CLIENT_COMMANDS::QUIT:
            // TODO: Call quit_handler();
            return "QUIT handler not yet implemented.";
        
        case CLIENT_COMMANDS::INVALID_COMMAND:
        default:
            // Should not reach here if parse_message() is correct
            return std::string(kCmdToStr.at(SERVER_RESPONSES::INVALID_COMMAND));
    }
}

/// @brief Builds a formatted POST_ERROR response.
/// @param errorMessage The error description to include.
/// @return A wire-format error response ready to send to client.
std::string build_post_error(const std::string& errorMessage) 
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


/// @brief Builds a formatted POST_OK response.
/// @return A string in TCP transmission format indicating a successful post.
std::string build_post_ok() 
{
    // Wire format: "POST_OK}}&{{"
    string okPost = std::string(kCmdToStr.at(SERVER_RESPONSES::POST_OK));

    std::string emptyAuthor = ""; // No author for OK
    std::string emptyTitle = "";  // No title for OK
    std::string emptyMessage = ""; // No message for OK

    // Build and return the complete response string
    string returnResult = 
        okPost + fieldDelimiter +
        emptyAuthor + fieldDelimiter +
        emptyTitle + fieldDelimiter +
        emptyMessage + transmissionTerminator;
    
    return returnResult;
}

#ifndef UNIT_TEST
int main()
{
    const string mockCommand = "GET_MESSAGES";

    /// @brief Mock message data for testing purposes.
    const string mockMessage = "Hello from the TCP Server! This is a mock message for demonstration purposes.";
    const string mockAuthor = "Mock Skywalker";
    const string mockTitle = "Mock Message Title";
    const string mockTCPMessage =  mockMessage + fieldDelimiter + mockAuthor + fieldDelimiter + mockTitle + transmissionTerminator;

    constexpr const char* SERVER_ADDR = "0.0.0.0"; // Listen on all interfaces
    constexpr int SERVER_PORT = 26500;

    int ListeningSocket;          // The socket used to listen for incoming connections
    int CommunicationSocket;      // The socket used for communication with the client

    std::string RxBuffer;   // A buffer to hold received data - accumulates data until full message is received
    std::string CompletedMessage;
    std::string transmissionString;

    struct sockaddr_in SvrAddr;  // Server address structure

    // Setup the server socket for TCP
    ListeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListeningSocket == INVALID_SOCKET)
    {
        std::cout << "Socket creation failed with error: " << strerror(errno) << std::endl;
        close(ListeningSocket);
        return 0;
    }

    // Configure binding settings and bind the socket
    SvrAddr.sin_family = AF_INET;           // Use the Internet address family
    SvrAddr.sin_addr.s_addr = INADDR_ANY;   // Accept connections from any address
    SvrAddr.sin_port = htons(27000);        // Set port to 27000
    if (bind(ListeningSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr)) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        std::cout << "ERROR: Failed to bind ServerSocket" << strerror(errno) << std::endl;
        return 0;
    }

    // Start listening on the configured socket
    if (listen(ListeningSocket, 1) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        std::cout << "ERROR: Failed to configure listen on ServerSocket" << strerror(errno)<< std::endl;
        return 0;
    }

    std::cout << "Server is listening for a connection on port 27000..." << std::endl;

    // Accept a connection on the socket - spin up a new socket for communication
    CommunicationSocket = SOCKET_ERROR;
    if ((CommunicationSocket = accept(ListeningSocket, NULL, NULL)) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        std::cout << "ERROR: Failed to accept connection on ServerSocket" << strerror(errno) << std::endl;
        return 0;
    }
    
    std::cout << "Client connected successfully!\n" << std::endl;    

    std::cout << "Waiting to receive data from client...\n" << std::endl;

    //recv(CommunicationSocket, RxBuffer, sizeof(RxBuffer), 0);

    bool result = read_message_until_terminator(
        CommunicationSocket,
        RxBuffer,
        transmissionTerminator,
        CompletedMessage
    );

    if(result )
   
    std::cout << "Received message from client: " << CompletedMessage << std::endl;   
    
    // Here is where we want to take the completed message and process it so we can route it accordingly

    // Build the transmission string safely using std::string and truncate to 115 characters if needed
    transmissionString = "Received: " + CompletedMessage.substr(0, 115) + ".";

    std::cout << "Sending '" << transmissionString << "' to client...\n" << std::endl;

    send(CommunicationSocket, transmissionString.c_str(), transmissionString.size(), 0);
    // Cleanup and close the sockets
    close(CommunicationSocket);
    close(ListeningSocket);
    std::cout << "Server shutdown successfully." << std::endl;
    return 0;
}
#endif