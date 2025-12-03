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

int main()
{
    /// @brief Delimits the fields in a message.
    const string fieldDelimiter = "}+{";

    /// @brief Terminates a complete message transmission.
    const string transmissionTerminator = "}}&{{";

    /// @brief Separates multiple messages in a transmission.
    const string messageSeperator = "}#{";

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

    struct sockaddr_in SvrAddr;  // Server address structure

    // char RxBuffer[128] = {};    // A buffer to hold received data
    // char TxBuffer[128] = {};    // A buffer to hold data to send

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
        RxBuffer
    );
   
    std::cout << "Received message from client: " << RxBuffer << std::endl;    

    snprintf(TxBuffer, sizeof(TxBuffer), "Received: %.115s.", RxBuffer);

	std::cout << "Sending '" << TxBuffer << "' to client...\n" << std::endl;

    send(CommunicationSocket, TxBuffer, sizeof(TxBuffer), 0);

    // Cleanup and close the sockets
    close(CommunicationSocket);
    close(ListeningSocket);
    std::cout << "Server shutdown successfully." << std::endl;
    return 0;
}




