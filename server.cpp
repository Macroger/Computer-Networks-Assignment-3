/*
** Filename: server.cpp
** Project: Computer Networks Assignment 3
** Author: Matthew G. Schatz, Kian Cloutier
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

using namespace std;

constexpr int INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

enum class COMMANDS
{
    GET_BOARD,          // A command sent by the client to get the message board
    POST,               // A command sent by the client to post a new message
    POST_OK,            // Server sends to client to confirm post was successful
    POST_ERROR,         // Server sends to client to indicate post was unsuccessful
    DELETE_POST,        // A command sent by the client to request deletion of a post - NOT IMPLEMENTED
    UPDATE_POST,        // A command sent by the client to request an update to a post - NOT IMPLEMENTED
    INVALID_COMMAND,    // Sent by server to client if command is not recognized
    QUIT                // Client indicates it is done and wants to close the connection
};

/// @brief Represents a message board post.
struct Post {
    int id;                // assigned by server storage
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
    COMMANDS cmd = COMMANDS::INVALID_COMMAND;
    std::vector<Post> posts;     // for POST
    std::string filter_author;   // for GET_BOARD (optional)
    std::string filter_title;    // for GET_BOARD (optional)
};

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

std::string commandToString(COMMANDS cmd)
{
    switch (cmd)
    {
        case COMMANDS::GET_BOARD:
            return "GET_BOARD";
        case COMMANDS::POST:
            return "POST";
        case COMMANDS::POST_OK:
            return "POST_OK";
        case COMMANDS::POST_ERROR:
            return "POST_ERROR";
        case COMMANDS::QUIT:
            return "QUIT";
        default:
            return "INVALID_COMMAND";
    }
}

std::string getBoardString(){
    return "board file not implented yet; for now enjoy this string";
};

bool addPostToBoard(std::string post){
    return true;
};

int main()
{
    const string fieldDelimiter = "}+{";
    const string transmissionTerminator = "}}&{{";
    const string messageSeperator = "}#{";
    const string mockCommand = "GET_MESSAGES";
    const string mockMessage = "Hello from the TCP Server! This is a mock message for demonstration purposes.";
    const string mockAuthor = "Mock Skywalker";
    const string mockTitle = "Mock Message Title";
    const string mockTCPMessage =  mockMessage + fieldDelimiter + mockAuthor + fieldDelimiter + mockTitle + transmissionTerminator;

    constexpr const char* SERVER_ADDR = "0.0.0.0";
    constexpr int SERVER_PORT = 26500;

    int ListeningSocket;
    int CommunicationSocket;

    std::string RxBuffer;
    std::string CompletedMessage;

    struct sockaddr_in SvrAddr;
    struct sockaddr_in ClientAddr;
    socklen_t ClientAddrLen = sizeof(ClientAddr);

    ListeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListeningSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed with error: " << strerror(errno) << std::endl;
        return 1;
    }

    int opt = 1;
    if (setsockopt(ListeningSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR)
    {
        std::cerr << "WARNING: Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
    }

    std::memset(&SvrAddr, 0, sizeof(SvrAddr));
    SvrAddr.sin_family = AF_INET;
    SvrAddr.sin_addr.s_addr = INADDR_ANY;
    SvrAddr.sin_port = htons(SERVER_PORT);
    if (bind(ListeningSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr)) == SOCKET_ERROR)
    {
        std::cerr << "ERROR: Failed to bind ServerSocket: " << strerror(errno) << std::endl;
        close(ListeningSocket);
        return 1;
    }

    if (listen(ListeningSocket, 5) == SOCKET_ERROR)
    {
        std::cerr << "ERROR: Failed to configure listen on ServerSocket: " << strerror(errno) << std::endl;
        close(ListeningSocket);
        return 1;
    }

    std::cout << "Server is listening for a connection on port " << SERVER_PORT << "..." << std::endl;

    while (true)
    {
        std::cout << "\nWaiting for client connection..." << std::endl;

        CommunicationSocket = accept(ListeningSocket, (struct sockaddr*)&ClientAddr, &ClientAddrLen);
        if (CommunicationSocket == SOCKET_ERROR)
        {
            std::cerr << "ERROR: Failed to accept connection on ServerSocket: " << strerror(errno) << std::endl;
            continue;
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ClientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        int clientPort = ntohs(ClientAddr.sin_port);
        
        std::cout << "Client connected: " << clientIP << ":" << clientPort << std::endl;

        RxBuffer.clear();
        CompletedMessage.clear();

        bool clientActive = true;
        while (clientActive)
        {
            std::cout << "Waiting to receive data from client..." << std::endl;

            bool receiveSuccess = read_message_until_terminator(
                CommunicationSocket,
                RxBuffer,
                transmissionTerminator,
                CompletedMessage
            );
           
            if (!receiveSuccess)
            {
                std::cerr << "Failed to receive complete message from client. Closing connection." << std::endl;
                clientActive = false;
                break;
            }

            std::cout << "Received message from client: " << CompletedMessage << std::endl;
            
            std::string response = "If u seeing this something went wrong lol" + transmissionTerminator;

            if (CompletedMessage.find(commandToString(COMMANDS::QUIT)) == 0) {
                std::cout << "Client requested to close the connection." << std::endl;
                response = "Closing connection" + transmissionTerminator;
                clientActive = false;
                break;
            } else if (CompletedMessage.find(commandToString(COMMANDS::GET_BOARD)) == 0) {
                std::cout << "Client requested the message board." << std::endl;
                // Display message board; rn just send a message to acknowledge
                response = getBoardString() + transmissionTerminator;

            } else if (CompletedMessage.find(commandToString(COMMANDS::POST)) == 0) {
                std::cout << "Client is posting a new message." << std::endl;
                // Post the message to the server's message board; for now just ack
                addPostToBoard(CompletedMessage);
                response = "Received post (POST_OK) (Post adding not implemented yet)" + transmissionTerminator;
            } else {
                std::cout << "Couldn't find command in client message" << std::endl;
                response = "Invalid or no command found in message" + transmissionTerminator;
            }
            
            std::cout << "Sending response to client..." << std::endl;
            
            ssize_t bytesSent = send_all_bytes(
                CommunicationSocket, 
                response.c_str(), 
                response.length(), 
                0
            );

            if (bytesSent == -1)
            {
                std::cerr << "Failed to send response to client. Closing connection." << std::endl;
                clientActive = false;
                break;
            }

            std::cout << "Response sent successfully (" << bytesSent << " bytes)." << std::endl;
            
            CompletedMessage.clear();
        }

        std::cout << "Closing connection with " << clientIP << ":" << clientPort << std::endl;
        close(CommunicationSocket);
        std::cout << "Connection closed." << std::endl;
    }

    close(ListeningSocket);
    std::cout << "Server shutdown successfully." << std::endl;
    return 0;
}

