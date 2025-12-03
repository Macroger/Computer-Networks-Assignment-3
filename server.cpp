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


constexpr const char* SERVER_ADDR = "0.0.0.0"; // or "127.0.0.1" or a specific IP (no CIDR)
constexpr int SERVER_PORT = 27000;


int main()
{
    int ListeningSocket;          // The socket used to listen for incoming connections
    int CommunicationSocket;      // The socket used for communication with the client

    sockaddr_in SvrAddr;        // The server address structure

    char RxBuffer[128] = {};
    char TxBuffer[128] = {};

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

    recv(CommunicationSocket, RxBuffer, sizeof(RxBuffer), 0);
   
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




