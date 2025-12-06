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

// ============================================================================
// SYSTEM INCLUDES
// ============================================================================

// Socket programming headers
#include <sys/types.h>       // Data types used in system calls
#include <sys/socket.h>      // Socket API functions
#include <netinet/in.h>      // Internet address structures
#include <arpa/inet.h>       // Internet address conversion utilities
#include <unistd.h>          // POSIX API (close, read, write, etc.)

// Standard C++ and system headers
#include <cstring>           // C-style string functions
#include <cerrno>            // Error number constants
#include <exception>         // Exception handling
#include <iostream>          // Input/output operations
#include <ctime>             // Time-related functions
#include <vector>            // Dynamic arrays
#include <algorithm>         // Standard algorithms (find, etc.)
#include <unordered_map>     // Hash map for command lookups
#include <string_view>       // Non-owning string references
#include <thread>            // Thread spawning and management
#include <mutex>             // Mutual exclusion locks

// Project-specific headers
#include "shared_state.h"    // Global shared server state

using namespace std;

// ============================================================================
// GLOBAL STATE AND CONSTANTS
// ============================================================================

// Instantiate global shared state that is accessed from multiple threads
// This contains the message board, event log, client tracking, and all synchronization primitives
SharedServerState g_serverState;

// Socket error constants (for platform compatibility)
constexpr int INVALID_SOCKET = -1;   // Invalid socket descriptor value
constexpr int SOCKET_ERROR = -1;     // Socket operation error return value

// ============================================================================
// PROTOCOL DELIMITERS AND WIRE FORMAT CONSTANTS
// ============================================================================
// These strings define the message protocol structure for TCP communication
// Format: "COMMAND}+{field1}+{field2}+{field3}#+{ NEXT_COMMAND}+{...}}&{{"

/// @brief Delimits the fields within a single message.
// Example: "POST}+{John}+{Hello}+{Hi there}"
const string fieldDelimiter = "}+{";

/// @brief Terminates a complete message transmission.
// Every complete message MUST end with this sequence
const string transmissionTerminator = "}}&{{";

/// @brief Separates multiple messages in a single transmission.
// Allows batching multiple POST messages: "POST}+{...}#{POST}+{...}"
const string messageSeperator = "}#{";

// ============================================================================
// CLIENT COMMAND ENUMERATION AND MAPPING
// ============================================================================

/// @brief Enumerates all valid commands that can be sent from client to server
enum class CLIENT_COMMANDS
{
    GET_BOARD,          // Client requests all messages (with optional filters)
    POST,               // Client posts one or more new messages
    INVALID_COMMAND,    // Unknown command received from client
    QUIT                // Client gracefully closes connection
};

/// @brief Maps command strings (from wire format) to CLIENT_COMMANDS enum values
// Used in parse_message() to convert received text to enum
const std::unordered_map<std::string_view, CLIENT_COMMANDS> kCmdFromStr{
  {"GET_BOARD", CLIENT_COMMANDS::GET_BOARD},
  {"POST",      CLIENT_COMMANDS::POST},
  {"INVALID_COMMAND", CLIENT_COMMANDS::INVALID_COMMAND},
  {"QUIT",      CLIENT_COMMANDS::QUIT},
};

// ============================================================================
// SERVER RESPONSE ENUMERATION AND MAPPING
// ============================================================================

/// @brief Enumerates all valid responses the server can send to clients
enum class SERVER_RESPONSES
{
    GET_BOARD,          // Server responds with message board data
    POST_OK,            // Server confirms post was successful
    POST_ERROR,         // Server reports post failed with error
    GET_BOARD_ERROR,    // Server reports get_board failed with error
    INVALID_COMMAND     // Server reports unrecognized command
};

/// @brief Maps SERVER_RESPONSES enum values to their wire format strings
// Used when building responses to send back to clients
const std::unordered_map<SERVER_RESPONSES, std::string_view> kCmdToStr{
    {SERVER_RESPONSES::POST_OK,    "POST_OK"},
    {SERVER_RESPONSES::POST_ERROR, "POST_ERROR"},
    {SERVER_RESPONSES::GET_BOARD,  "GET_BOARD"},
    {SERVER_RESPONSES::GET_BOARD_ERROR, "GET_BOARD_ERROR"},
    {SERVER_RESPONSES::INVALID_COMMAND, "INVALID_COMMAND"},
};

// ============================================================================
// PARSE RESULT STRUCTURE
// ============================================================================

/// @brief Represents the result of parsing a client message.
/// Contains either the parsed command and associated data, or an error message.
/// This acts as a "variant" to hold either success data or failure details.
struct ParseResult {
    bool ok;                                    // True if parsing succeeded, false if error occurred
    std::string error;                          // Non-empty string only on failure; describes the parse error
    CLIENT_COMMANDS clientCmd = CLIENT_COMMANDS::INVALID_COMMAND;  // The parsed command type
    std::vector<Post> posts;                    // For POST command: array of (author, title, message) triples
    std::string filter_author;                  // For GET_BOARD command: optional author filter
    std::string filter_title;                   // For GET_BOARD command: optional title filter
};

// ============================================================================
// POST COMMAND HANDLER
// ============================================================================

/// @brief Handles the POST command - adds new messages to the shared message board
/// @param parsed The ParseResult containing POST data (array of Post objects)
/// @param errorDetails Output parameter: set to error message if operation fails
/// @param clientId The unique ID of the client posting these messages
/// @return True if all posts were added successfully; false if an error occurred
bool post_handler(const ParseResult& parsed, std::string& errorDetails, int clientId)
{
    // DEBUG: Print what's being posted
    // std::cout << "\n=== POST_HANDLER DEBUG ===" << std::endl;
    // std::cout << "Number of posts to add: " << parsed.posts.size() << std::endl;
    
    // Append each post from the parsed result to the shared message board
    try{
        // Validate that there are posts to add
        if (parsed.posts.empty()) {
            errorDetails = "No posts to add";
            return false; // No posts to add
        }

        // Acquire exclusive lock to safely modify the shared message board
        // All threads will wait for this lock before modifying messageBoard
        std::lock_guard<std::mutex> lock(g_serverState.boardMutex);

        // Add each post from the parsed array to the shared message board
        for (size_t i = 0; i < parsed.posts.size(); i++)
        {
            Post p = parsed.posts[i];
            // DEBUG: Detailed post addition logging
            // std::cout << "  Adding Post " << i << ": Author=\"" << p.author 
            //           << "\" Title=\"" << p.title 
            //           << "\" Message=\"" << p.message << "\"" << std::endl;
            
            // Associate this post with the client that posted it
            p.clientId = clientId;
            
            // Add the post to the shared message board (thread-safe under the lock)
            g_serverState.messageBoard.push_back(p);
            
            // Increment the total message counter for statistics
            g_serverState.totalMessagesReceived++;
        }
        
        // DEBUG: Verify posts were added
        // std::cout << "Total posts in messageBoard after adding: " << g_serverState.messageBoard.size() << std::endl;
        // std::cout << "==========================\n" << std::endl;
    } 
    catch (const std::exception& ex) 
    {
        // Catch any exceptions that occur during post addition (e.g., memory issues)
        errorDetails = std::string("Exception occurred while adding posts: ") + ex.what();
        return false; // Signal failure to caller
    } 
    catch (...) 
    {
        // Catch any unknown exceptions
        errorDetails = "Unknown exception occurred while adding posts";
        return false;
    }

    // All posts added successfully
    return true;
}

// ============================================================================
// QUIT COMMAND HANDLER (DEPRECATED - HANDLED IN CLIENT HANDLER)
// ============================================================================

/// @brief Handles client disconnect (currently deprecated as logic moved to client_handler)
/// @param SocketToClose The client socket to close
/// @return True on success
bool quit_handlder(int SocketToClose)
{
    // Perform any necessary cleanup for the client session
    // Note: This function is mostly unused; QUIT is handled in client_handler instead

    // Immediately close the socket - since the client requested to quit
    close(SocketToClose);
    return true;
}

// ============================================================================
// POST_OK RESPONSE BUILDER
// ============================================================================

/// @brief Builds a formatted POST_OK response in wire format
/// Wire format for success: "POST_OK}+{}+{}+{}}&{{"
/// (Empty fields since OK response has no data payload)
/// @return A formatted string ready to send to the client
std::string build_post_ok()
{
    // Get the string representation of the POST_OK response code
    string okPost = std::string(kCmdToStr.at(SERVER_RESPONSES::POST_OK));

    // For a successful POST, we don't send any data back - just empty fields
    std::string author = "";  // Empty: no author in success response
    std::string title = "";   // Empty: no title in success response
    std::string message = ""; // Empty: no message in success response

    // Assemble the complete response using the wire format protocol
    // Format: COMMAND}+{field1}+{field2}+{field3}}&{{
    string returnResult = 
        okPost + fieldDelimiter +        // Response command
        author + fieldDelimiter +        // Empty author field
        title + fieldDelimiter +         // Empty title field
        message + transmissionTerminator; // Empty message field + end marker
    
    return returnResult;
}

// ============================================================================
// MESSAGE FIELD SPLITTING UTILITY
// ============================================================================

/// @brief Splits a message string into fields based on a delimiter
/// Only processes up to endPos, allowing partial message parsing
/// This is used to isolate the actual message from any buffered data that follows it
/// @param text The input string to split
/// @param delim The delimiter string that separates fields (e.g., "}+{")
/// @param endPos The position in the string up to which to process (acts as a limit)
/// @return A vector of split field strings
static std::vector<std::string> split_fields_until(const std::string& text, const std::string& delim, size_t endPos) 
{
    // Vector to accumulate the extracted fields
    std::vector<std::string> out;

    // Current parsing position in the string
    size_t start = 0;

    // Process fields until we reach endPos (stopping point)
    while (start <= endPos) 
    {
        // Find the next delimiter starting from current position
        size_t p = text.find(delim, start);

        // If delimiter not found OR delimiter is beyond endPos
        // Extract from current position to endPos and finish
        if (p == std::string::npos || p > endPos) 
        {
            // Extract the final field (from start to endPos)
            out.push_back(text.substr(start, endPos - start));
            break;
        }

        // Delimiter found within bounds: extract field from start to delimiter
        out.push_back(text.substr(start, p - start));
        
        // Move start position past the delimiter for next iteration
        start = p + delim.size();
    }

    // Return all extracted fields
    return out;
}

// ============================================================================
// GET_BOARD COMMAND HANDLER
// ============================================================================

/// @brief Handles the GET_BOARD command - returns the message board, optionally filtered
/// Retrieves all posts from the message board and formats them in wire format
/// Can optionally filter by author name and/or title
/// @param authorFilter Optional filter: only return posts by this author (empty = no filter)
/// @param titleFilter Optional filter: only return posts with this exact title (empty = no filter)
/// @return A formatted wire-format string containing the filtered message board
std::string get_board_handler(const std::string& authorFilter, const std::string& titleFilter)
{
    // Acquire exclusive lock to safely read from the shared message board
    // Prevents other threads from modifying messageBoard while we're reading it
    std::lock_guard<std::mutex> lock(g_serverState.boardMutex);
    
    // DEBUG: Detailed board state logging
    // std::cout << "\n=== GET_BOARD_HANDLER DEBUG ===" << std::endl;
    // std::cout << "Total posts in messageBoard: " << g_serverState.messageBoard.size() << std::endl;
    // for (size_t i = 0; i < g_serverState.messageBoard.size(); i++) {
    //     std::cout << "  Post " << i << ": Author=\"" << g_serverState.messageBoard[i].author 
    //               << "\" Title=\"" << g_serverState.messageBoard[i].title 
    //               << "\" Message=\"" << g_serverState.messageBoard[i].message << "\"" << std::endl;
    // }
    // std::cout << "Filters: Author=\"" << authorFilter << "\" Title=\"" << titleFilter << "\"" << std::endl;
    
    // Build a single response string containing all posts matching the optional filters
    // Wire format: "GET_BOARD}+{author1}+{title1}+{msg1}#{author2}+{title2}+{msg2}}&{{"
    std::string allMessages;
    std::string command = std::string(kCmdToStr.at(SERVER_RESPONSES::GET_BOARD));

    // Start the message with the GET_BOARD command identifier
    allMessages += command;

    // Iterate through all posts in the message board and append matching ones to the response
    // Use message separator }#{ between posts and field delimiter }+{ within post data
    bool firstPost = true;           // Track if this is the first post (no separator needed)
    int postsIncluded = 0;           // Count how many posts matched the filters
    for (const Post& post : g_serverState.messageBoard)
    {
        // Apply author filter: skip if authorFilter is set and doesn't match post's author
        if (!authorFilter.empty() && post.author != authorFilter) continue;
        
        // Apply title filter: skip if titleFilter is set and doesn't match post's title
        if (!titleFilter.empty() && post.title != titleFilter) continue;
        
        // Add message separator BEFORE each post except the first one
        // This follows the wire format where posts are separated by }#{
        if (!firstPost) {
            allMessages += messageSeperator;
        }
        
        // Mark that we've added at least one post (so separators are needed before subsequent posts)
        firstPost = false;
        postsIncluded++;  // Increment counter for statistics
        
        // Append this post's data in wire format: }+{author}+{title}+{message
        allMessages += fieldDelimiter + post.author + fieldDelimiter + post.title + fieldDelimiter + post.message;
    }

    // DEBUG: Verify response assembly
    // std::cout << "Posts included in response: " << postsIncluded << std::endl;
    // std::cout << "Response wire format: " << allMessages << std::endl;
    // std::cout << "================================\n" << std::endl;

    // Append the transmission terminator to mark the end of this response
    allMessages += transmissionTerminator;

    // Return the complete formatted response
    return allMessages;
}

// ============================================================================
// MESSAGE PARSING FUNCTION
// ============================================================================

/// @brief Parses a complete message received from a client
/// Handles variable field delimiters and message separators
/// Returns a ParseResult with either parsed data or error details
/// @param completeMessage The raw message string to parse (must include terminator)
/// @param fieldDelimiter String used to separate fields (normally "}+{")
/// @param messageSeperator String used to separate multiple messages (normally "}#{")
/// @param transmissionTerminator String marking end of transmission (normally "}}&{{")
/// @return ParseResult containing parsed command/data or error details
ParseResult parse_message(const std::string& completeMessage,
                          const std::string& fieldDelimiter,
                          const std::string& messageSeperator,
                          const std::string& transmissionTerminator)
{
    ParseResult res{};     // Initialize result structure with defaults
    res.ok = false;        // Assume failure until parsing succeeds

    // DEBUG: Detailed message parsing logging
    // std::cout << "\n=== PARSE_MESSAGE DEBUG ===" << std::endl;
    // std::cout << "Raw message received (" << completeMessage.size() << " bytes):" << std::endl;
    // std::cout << "  [" << completeMessage << "]" << std::endl;
    
    // Quick validation: cannot parse an empty message
    if (completeMessage.empty()) {
        res.error = "Empty message received.";
        return res;
    }

    // Find the terminator position in the message (marks end of the logical message)
    // The terminator separates this message from any buffered data that follows
    size_t termPos = completeMessage.find(transmissionTerminator);

    // DEBUG: Log terminator position
    // std::cout << "Terminator pos: " << (termPos == std::string::npos ? -1 : (int)termPos) << std::endl;

    // Determine where to stop parsing: use terminator if found, otherwise full message
    // This ensures we only parse the current complete message, not partial buffered data
    size_t endPos = completeMessage.size();
    if (termPos != std::string::npos) {
        endPos = termPos;  // Stop at the terminator
    }

    // DEBUG: Log parsing bounds
    // std::cout << "Using endPos: " << endPos << std::endl;
    // std::cout << "Message chunk to parse: [" << completeMessage.substr(0, endPos) << "]" << std::endl;

    // ====================================================================
    // FIELD EXTRACTION: Convert message to tokenizable format
    // ====================================================================
    // Wire format uses message separators }#{ to delimit individual messages within
    // a batch, but for parsing, we can treat them as regular field delimiters
    // Strategy: temporarily replace message separators with field delimiters
    std::string messageToTokenize = completeMessage.substr(0, endPos);
    size_t replacePos = 0;
    while ((replacePos = messageToTokenize.find(messageSeperator, replacePos)) != std::string::npos) {
        // Replace }#{ with }+{ to normalize all delimiters
        messageToTokenize.replace(replacePos, messageSeperator.length(), fieldDelimiter);
        replacePos += fieldDelimiter.length();
    }
    
    // Split the normalized message into individual fields
    auto fields = split_fields_until(messageToTokenize, fieldDelimiter, messageToTokenize.size());
    if (fields.empty()) {
        res.error = "Malformed message: no fields found.";
        return res;  // Cannot proceed without at least a command
    }

    // ====================================================================
    // COMMAND PARSING (First field is always the command)
    // ====================================================================
    const std::string& commandStr = fields[0];
   
    // Look up command string in the string-to-enum map
    // This converts wire format command strings (e.g., "GET_BOARD") to enum values
    auto it = kCmdFromStr.find(commandStr);

    // Check if command was found in the map
    bool found = (it != kCmdFromStr.end());

    // Set the parsed command in the result
    if (found) 
    {
        res.clientCmd = it->second;  // Map the string to the enum value
    } 
    else 
    {
        // Unknown command - mark as invalid and return error
        res.clientCmd = CLIENT_COMMANDS::INVALID_COMMAND;
        res.error = "Invalid command: " + commandStr;
        return res;
    }

    // ====================================================================
    // PAYLOAD PARSING (depends on command type)
    // ====================================================================
    
    // GET_BOARD: Optional filters for author and title
    if (res.clientCmd == CLIENT_COMMANDS::GET_BOARD) 
    {
        // Extract optional filter parameters from remaining fields
        // Format: GET_BOARD}+{[author]}+{[title]}
        if (fields.size() > 1) {
            res.filter_author = fields[1];  // Optional author filter
        }
        if (fields.size() > 2) {
            res.filter_title = fields[2];   // Optional title filter
        }
        res.ok = true;  // Successfully parsed GET_BOARD
        return res;
    }

    // POST: One or more (author, title, message) triples
    if (res.clientCmd == CLIENT_COMMANDS::POST) 
    {
        // Payload is everything after the command field
        // Each post is: author}+{title}+{message
        const size_t payloadCount = fields.size() - 1;  // Exclude command field
        
        // Validate payload structure: must have at least one triple
        // And total fields must be divisible by 3 (author, title, message per post)
        if (payloadCount == 0) 
        {
            res.error = "POST contains no (Author, Title, Message) sets.";
            return res;
        }

        // Verify payload forms complete triples (no partial posts)
        if (payloadCount % 3 != 0) 
        {
            res.error = "POST requires triples of Author, Title, Message.";
            return res;
        }

        // ================================================================
        // Extract individual posts from the triples
        // ================================================================
        // Loop through fields in groups of 3 (author, title, message)
        for (size_t i = 1; i + 2 < fields.size(); i += 3) 
        {
            const std::string& author  = fields[i];     // Index i = author (may be empty for anonymous)
            const std::string& title   = fields[i+1];   // Index i+1 = title (may be empty)
            const std::string& message = fields[i+2];   // Index i+2 = message body

            // Validation: message content cannot be empty
            if (message.empty()) 
            { 
                res.error = "POST message cannot be empty."; 
                return res;  // Reject post with empty message
            }

            // Create a new Post object from the fields
            // Author and title CAN be empty, but message cannot
            Post p{author, title, message};

            // Add the post to the result (uses move semantics for efficiency)
            res.posts.push_back(std::move(p));
        }

        // All posts extracted successfully
        res.ok = true;
        // DEBUG: Log parsed posts
        // std::cout << "Parsed " << res.posts.size() << " POST messages" << std::endl;
        // std::cout << "===========================\n" << std::endl;
        return res;
    }

    // QUIT: No payload needed, just the command
    if (res.clientCmd == CLIENT_COMMANDS::QUIT) 
    {
        res.ok = true;  // Successfully parsed QUIT
        // DEBUG: Log QUIT parsing
        // std::cout << "Parsed QUIT command" << std::endl;
        // std::cout << "===========================\n" << std::endl;
        return res;
    }

    // Unhandled command type (should not reach here due to earlier checks)
    res.error = "Unhandled command or parsing error.";
    // DEBUG: Log error
    // std::cout << "===========================\n" << std::endl;
    return res;
}

// ============================================================================
// SOCKET I/O FUNCTIONS
// ============================================================================

/// @brief Sends all bytes in a buffer through a socket (handles partial sends)
/// The system may not send all requested bytes in a single send() call
/// This function loops until all bytes are sent or an error occurs
/// @param socket The socket file descriptor to send through
/// @param buffer Pointer to the data buffer to transmit
/// @param length The total number of bytes to send from the buffer
/// @param flags Flags to modify send() behavior (usually 0)
/// @return Total number of bytes successfully sent, or -1 on error
ssize_t send_all_bytes(int socket, const char* buffer, size_t length, int flags)
{
    size_t totalSent = 0;  // Track total bytes sent so far
    
    // Keep sending until all bytes have been transmitted
    while (totalSent < length)
    {
        // Attempt to send remaining bytes
        // buffer + totalSent: pointer to remaining data
        // length - totalSent: number of bytes remaining to send
        ssize_t bytesSent = send(socket, buffer + totalSent, length - totalSent, flags);
        
        // Check if send was successful
        if (bytesSent > 0)
        {
            // Advance total counter and continue with remaining bytes
            totalSent += bytesSent;
            continue;  // Loop to send more
        }
        
        // Non-blocking send interrupted by signal - safe to retry
        if (bytesSent && errno == EINTR)
        {
            continue; // Retry the send after signal
        }
        
        // Actual error occurred (not signal interrupt)
        std::cerr << "Error sending data: " << strerror(errno) << std::endl;
        return -1;  // Signal error to caller
    }
    
    // All bytes successfully sent
    return totalSent;
}

// ============================================================================
// MESSAGE RECEPTION AND BUFFERING
// ============================================================================

/// @brief Reads data from socket until a terminator sequence is found
/// Uses buffering to handle cases where terminator arrives in multiple recv() calls
/// Properly handles partial messages and removes processed data from buffer
/// @param socket The socket to read from
/// @param messageBuffer Accumulation buffer for received data (updated with remaining data)
/// @param terminator The sequence that marks end of a complete message
/// @param completedMessage Output: the extracted complete message (without terminator)
/// @return True if a complete message was successfully read; false on error/disconnect
bool read_message_until_terminator(
    int socket,
    std::string& messageBuffer,
    const std::string& terminator,
    std::string &completedMessage
)
{
    // ====================================================================
    // CHECK IF MESSAGE ALREADY IN BUFFER
    // ====================================================================
    // Optimization: maybe we already have a complete message in the buffer
    // from a previous recv() call (buffered before this function was called)
    auto pos = messageBuffer.find(terminator);
    if (pos != std::string::npos)
    {
        // Extract complete message up to (but not including) terminator
        completedMessage = messageBuffer.substr(0, pos);
        
        // Remove processed message AND terminator from buffer
        // This leaves any subsequent data for the next message
        messageBuffer.erase(0, pos + terminator.size());
        return true;  // Success: found complete message
    }

    // ====================================================================
    // READ FROM SOCKET UNTIL TERMINATOR FOUND
    // ====================================================================
    char temp[4096] = {};  // Temporary buffer for receiving data from socket
    
    while(true)
    {
        // Attempt to receive data from the socket
        ssize_t bytesReceived = recv(socket, temp, sizeof(temp), 0);
        
        // Success: got data from socket
        if (bytesReceived > 0)
        {
            // Append received data to the accumulation buffer
            messageBuffer.append(temp, bytesReceived);

            // Check if terminator is now present in the accumulated buffer
            auto pos = messageBuffer.find(terminator);
            if (pos != std::string::npos)
            {
                // Found terminator! Extract message and update buffer
                completedMessage = messageBuffer.substr(0, pos);
                messageBuffer.erase(0, pos + terminator.size());
                return true;  // Successfully extracted complete message
            }

            // Terminator not found yet, continue receiving more data
            continue;
        }

        // recv() returned 0: peer closed connection gracefully
        if (bytesReceived == 0)
        {
            // Connection has been closed by the client
            std::cerr << "Connection closed by peer." << std::endl;
            return false;  // Signal error: connection closed
        }

        // recv() returned -1: check if it's a retriable error
        if (errno == EINTR)
        {
            continue; // Interrupted by signal, retry the recv
        }

        // Actual error occurred (not signal interrupt)
        std::cerr << "Error receiving data: " << strerror(errno) << std::endl;
        return false;  // Signal error to caller
    }
}

// ============================================================================
// ERROR RESPONSE BUILDERS
// ============================================================================

/// @brief Builds a formatted POST_ERROR response in wire format
/// Returns error message to client when POST operation fails
/// Wire format: "POST_ERROR}+{}+{}+{error_message}}&{{"
/// @param errorMessage The error description to include in response
/// @return A formatted wire-format error response ready to send
std::string handle_post_error(const std::string& errorMessage) 
{
    // Get the error response command string
    string errorPost = std::string(kCmdToStr.at(SERVER_RESPONSES::POST_ERROR));

    // For error responses, fill empty fields but include the error message
    string emptyAuthor = "";  // No author field in error response
    string emptyTitle = "";   // No title field in error response

    // Assemble complete error response using wire format
    // Format: POST_ERROR}+{}+{}+{error_message}}&{{
    string returnResult = 
        errorPost + fieldDelimiter +        // Response command
        emptyAuthor + fieldDelimiter +      // Empty author field
        emptyTitle + fieldDelimiter +       // Empty title field
        errorMessage + transmissionTerminator; // Error message + terminator
    
    return returnResult;
}

// ============================================================================
// CLIENT REQUEST DISPATCHER AND HANDLER
// ============================================================================

/// @brief Routes parsed client requests to appropriate handlers and sends responses
/// Executes command handlers (POST, GET_BOARD, etc.) and constructs wire-format responses
/// Logs all activity to the shared event log for the GUI to display
/// @param parsed The ParseResult containing parsed command and payload
/// @param CommunicationSocket The socket for communication with this client
/// @param clientId The unique identifier assigned to this client on connection
void handle_client_request(const ParseResult& parsed, int CommunicationSocket, int clientId)
{
    // ====================================================================
    // ERROR CHECK: Validate parsing was successful
    // ====================================================================
    if (!parsed.ok) 
    {
        // Parsing failed - send invalid command response back to client
        g_serverState.logEvent("ERROR", "Invalid command: " + parsed.error);
        std::string emptyAuthor = "";
        std::string emptyTitle = "";
        std::string response = "INVALID_COMMAND" + fieldDelimiter + emptyAuthor + fieldDelimiter + emptyTitle + fieldDelimiter + parsed.error + transmissionTerminator;
        send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
        return;  // Done handling this invalid request
    }

    // ====================================================================
    // COMMAND DISPATCHER
    // ====================================================================
    // Route the request to the appropriate handler based on command type
    switch (parsed.clientCmd) {

        // ================================================================
        // GET_BOARD COMMAND
        // ================================================================
        case CLIENT_COMMANDS::GET_BOARD: 
        {
            // Client requested the message board with optional filters
            // Reconstruct raw message for event log display
            std::string raw_msg = "GET_BOARD}+{" + parsed.filter_author + "}+{" + parsed.filter_title + "}}&{{";
            g_serverState.logEvent("GET_BOARD", "Client requested board (socket: " + std::to_string(CommunicationSocket) + ")", raw_msg);
            
            // Get the formatted message board (with optional filters applied)
            std::string response = get_board_handler(parsed.filter_author, parsed.filter_title);
            
            // Send the board to the client
            send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            return;     
        }

        // ================================================================
        // POST COMMAND
        // ================================================================
        case CLIENT_COMMANDS::POST: 
        {
            // Client posted one or more new messages
            std::string errorMessage;  // Buffer for error details if post fails
            
            // Try to add the posts to the shared message board
            bool result = post_handler(parsed, errorMessage, clientId);
            
            if (result == false)
            {
                // Post failed - send error response
                g_serverState.logEvent("POST_ERROR", errorMessage);
                std::string response = handle_post_error(errorMessage);
                send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            }
            else
            {
                // Post succeeded - send confirmation response
                // Reconstruct raw message from parsed posts for event log
                std::string raw_msg = "POST";
                for (const auto& post : parsed.posts) {
                    raw_msg += "}+{" + post.author + "}+{" + post.title + "}+{" + post.message;
                    if (&post != &parsed.posts.back()) raw_msg += "}#{";
                }
                raw_msg += "}}&{{";
                g_serverState.logEvent("POST", "Client posted " + std::to_string(parsed.posts.size()) + " message(s) (socket: " + std::to_string(CommunicationSocket) + ")", raw_msg);
                
                // Send success response
                std::string response = build_post_ok();
                send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            }
            return;
        }

        // ================================================================
        // QUIT COMMAND
        // ================================================================
        case CLIENT_COMMANDS::QUIT:
            // Note: QUIT is actually handled in the main client handler loop
            // This case should not be reached since client_handler breaks on QUIT
            return;

        // ================================================================
        // INVALID OR UNKNOWN COMMAND
        // ================================================================
        case CLIENT_COMMANDS::INVALID_COMMAND:
        default: {
            // Send generic invalid command response
            std::string emptyAuthor = "";
            std::string emptyTitle = "";
            std::string message = "Error, unable to interpret command - make sure to use accepted legitimate commands!";
            std::string response = "INVALID_COMMAND" + fieldDelimiter + emptyAuthor + fieldDelimiter + emptyTitle + fieldDelimiter + message + transmissionTerminator;
            send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            return;
        }
    }
}

// ============================================================================
// PER-CLIENT CONNECTION HANDLER (RUNS IN SEPARATE THREAD)
// ============================================================================

/// @brief Handles all communication with a single connected client
/// Runs in its own thread to allow simultaneous handling of multiple clients
/// Manages receive, parse, process, and response cycle for each client
/// Logs all client activity (connect, disconnect, requests) to event log
/// @param CommunicationSocket The socket for this client connection (passed by value)
void client_handler(int CommunicationSocket)
{
    std::string RxBuffer;        // Accumulation buffer for received data fragments
    std::string CompletedMessage; // Complete message once terminator is found
    bool keepRunning = true;     // Flag to control the client loop

    // ====================================================================
    // CLIENT CONNECTION INITIALIZATION
    // ====================================================================
    
    // Assign a unique ID to this client for tracking and logging
    int myClientId;
    {
        // Lock mutex to safely modify shared client tracking data
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        
        // Assign next available client ID (increments for each new client)
        myClientId = g_serverState.nextClientId++;
        
        // Add this client's socket to the active clients list
        g_serverState.activeClientSockets.push_back(CommunicationSocket);
    }

    // Increment the active connection counter for statistics
    g_serverState.activeConnections++;
    
    // Log the client connection event for the GUI
    g_serverState.logEvent("CONNECT", "Client #" + std::to_string(myClientId) + " connected (socket: " + std::to_string(CommunicationSocket) + ")");

    // ====================================================================
    // MAIN CLIENT MESSAGE LOOP
    // ====================================================================
    
    while (keepRunning) {

        // ================================================================
        // RECEIVE MESSAGE FROM CLIENT
        // ================================================================
        
        // Read from socket until we find a complete message (marked by terminator)
        bool result = read_message_until_terminator(
            CommunicationSocket,
            RxBuffer,                    // Accumulation buffer for partial data
            transmissionTerminator,      // Message end marker
            CompletedMessage             // Output: complete message received
        );

        // Check if read was successful or if connection closed
        if (!result) {
            // Connection closed or error reading - exit client loop
            g_serverState.logEvent("DISCONNECT", "Client disconnected (socket: " + std::to_string(CommunicationSocket) + ")");
            keepRunning = false;
            break;  // Exit message loop
        }

        // DEBUG: Log received message
        // std::cout << "Received message from client: " << CompletedMessage << std::endl;

        // ================================================================
        // PARSE MESSAGE FROM CLIENT
        // ================================================================
        
        // Parse the complete message to extract command and payload
        // Returns ParseResult with either parsed data or error details
        ParseResult parsed = parse_message(
            CompletedMessage,             // Complete message (no partial data)
            fieldDelimiter,               // Field separator
            messageSeperator,             // Message batch separator
            transmissionTerminator        // End marker
        );

        // ================================================================
        // CHECK FOR QUIT COMMAND (SPECIAL CASE)
        // ================================================================
        
        // Client requested graceful disconnect
        if (parsed.ok && parsed.clientCmd == CLIENT_COMMANDS::QUIT) {
            // Log the quit request
            g_serverState.logEvent("QUIT", "Client requested disconnect (socket: " + std::to_string(CommunicationSocket) + ")");
            
            // Send goodbye message back to client (as wire-format message)
            std::string emptyAuthor = "SERVER";
            std::string emptyTitle = "BYE!!!";
            std::string message = "Server says: BYE!!!";
            std::string response = "QUIT" + fieldDelimiter + emptyAuthor + fieldDelimiter + emptyTitle + fieldDelimiter + message + transmissionTerminator;
            send_all_bytes(CommunicationSocket, response.c_str(), response.size(), 0);
            
            // Exit the client loop
            keepRunning = false;
            break;  // End client session
        }

        // ================================================================
        // HANDLE THE CLIENT REQUEST
        // ================================================================
        
        // Route the parsed request to the appropriate handler
        // (GET_BOARD, POST, etc.) which generates and sends response
        handle_client_request(parsed, CommunicationSocket, myClientId);

        // ================================================================
        // PREPARE FOR NEXT MESSAGE
        // ================================================================
        
        // Clear the completed message for next iteration
        CompletedMessage.clear();
    }

    // ====================================================================
    // CLIENT CLEANUP AND DISCONNECTION
    // ====================================================================
    
    // Close the socket for this client
    close(CommunicationSocket);
    
    // Remove this client from active clients list
    {
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        
        // Find and remove this socket from the active list
        auto it = std::find(g_serverState.activeClientSockets.begin(), 
                           g_serverState.activeClientSockets.end(), 
                           CommunicationSocket);
        if (it != g_serverState.activeClientSockets.end()) {
            g_serverState.activeClientSockets.erase(it);
        }
    }
    
    // Decrement the active connection counter
    g_serverState.activeConnections--;
    
    // Thread will exit here (implicit return)
}

// ============================================================================
// SERVER MAIN LOOP
// ============================================================================

/// @brief Main server event loop - listens for connections and spawns client handlers
/// Manages TCP socket setup, binding, listening, and client connection acceptance
/// Runs in a background thread while GUI runs in main thread
/// Uses g_serverState.serverRunning flag to determine when to initiate shutdown
void server_run_loop()
{
    // Server configuration constants
    constexpr int SERVER_PORT = 26500;  // Port to listen on
    // constexpr const char* SERVER_ADDR = "0.0.0.0"; // Listen on all interfaces

    int ListeningSocket;          // Socket used to listen for incoming connections
    int CommunicationSocket;      // Socket for client communication (created on accept)

    struct sockaddr_in SvrAddr;   // Server address structure (holds IP/port binding info)

    // ====================================================================
    // SOCKET CREATION
    // ====================================================================
    
    // Create a TCP socket for listening (AF_INET = IPv4, SOCK_STREAM = TCP)
    ListeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListeningSocket == INVALID_SOCKET)
    {
        // Socket creation failed - log error and exit server
        g_serverState.logEvent("ERROR", "Socket creation failed: " + std::string(strerror(errno)));
        close(ListeningSocket);
        return;
    }

    // ====================================================================
    // SOCKET OPTIONS
    // ====================================================================
    
    // Set SO_REUSEADDR to allow port reuse after server restart
    // Without this, the port might be in TIME_WAIT state and unavailable for ~30 seconds
    int reuse = 1;
    if (setsockopt(ListeningSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        g_serverState.logEvent("WARNING", "Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
    }

    // ====================================================================
    // SOCKET BINDING
    // ====================================================================
    
    // Configure the server address structure for binding
    SvrAddr.sin_family = AF_INET;             // IPv4 address family
    SvrAddr.sin_addr.s_addr = INADDR_ANY;     // Listen on all network interfaces (0.0.0.0)
    SvrAddr.sin_port = htons(SERVER_PORT);    // Port 26500 (host-to-network byte order)
    
    // Bind the socket to the configured address and port
    if (bind(ListeningSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr)) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        g_serverState.logEvent("ERROR", "Failed to bind ServerSocket: " + std::string(strerror(errno)));
        return;
    }

    // ====================================================================
    // START LISTENING
    // ====================================================================
    
    // Put the socket in listening mode (backlog of 1 connection)
    if (listen(ListeningSocket, 1) == SOCKET_ERROR)
    {
        close(ListeningSocket);
        g_serverState.logEvent("ERROR", "Failed to configure listen on ServerSocket: " + std::string(strerror(errno)));
        return;
    }

    // Log that server is ready to accept connections
    g_serverState.logEvent("SERVER", "Server is listening for connections on port 26500...");

    // ====================================================================
    // MAIN ACCEPTANCE LOOP
    // ====================================================================
    
    // Accept client connections and spawn handler threads
    // Vector to hold references to client threads (not used since we detach, but could be extended)
    std::vector<std::thread> clientThreads;
    
    // Continue accepting connections while server is running
    while (g_serverState.serverRunning) {
        // Accept an incoming connection
        // Creates a new socket for communication with the client
        CommunicationSocket = accept(ListeningSocket, NULL, NULL);
        
        // Check if accept succeeded
        if (CommunicationSocket == SOCKET_ERROR)
        {
            // Accept failed - log warning but continue listening
            g_serverState.logEvent("WARNING", "Failed to accept connection on ServerSocket: " + std::string(strerror(errno)));
            continue;  // Keep trying to accept more connections
        }
        
        // ================================================================
        // SPAWN CLIENT HANDLER THREAD
        // ================================================================
        
        // Create new thread to handle this client
        // Each client gets its own thread for concurrent handling
        // We use detach() since we don't need to wait for the thread to finish
        // The thread will clean itself up when the client disconnects
        std::thread t(client_handler, CommunicationSocket);
        t.detach();  // Let thread run independently
    }

    // ====================================================================
    // GRACEFUL SERVER SHUTDOWN
    // ====================================================================
    
    // Server shutdown initiated (serverRunning set to false by GUI)
    g_serverState.logEvent("SERVER", "Initiating server shutdown - disconnecting all clients...");
    
    // ================================================================
    // GOODBYE MESSAGE BROADCAST
    // ================================================================
    
    // Notify all connected clients that server is shutting down
    {
        std::lock_guard<std::mutex> lock(g_serverState.clientsMutex);
        
        // Get snapshot of all currently connected clients
        std::vector<int> clientsToDisconnect = g_serverState.activeClientSockets;
        
        // Build goodbye message in wire format
        std::string goodbyeMessage = "SERVER" + fieldDelimiter + "SHUTDOWN" + fieldDelimiter + "Server is shutting down" + transmissionTerminator;
        
        // Send goodbye message multiple times to increase likelihood client threads receive it
        // Not because TCP is unreliable (it guarantees delivery), but because:
        // 1. Client handler threads may be blocked on recv() and need time to process
        // 2. We're about to close sockets forcefully, which can interrupt in-flight data
        for (int attempt = 0; attempt < 3; attempt++) {
            for (int clientSocket : clientsToDisconnect) {
                send_all_bytes(clientSocket, goodbyeMessage.c_str(), goodbyeMessage.size(), 0);
            }
            
            // Add delay between attempts to allow client threads time to process
            if (attempt < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
    
    // Give clients time to receive and process the goodbye message
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // ================================================================
    // CLEANUP
    // ================================================================
    
    // Close the listening socket (client sockets will be closed by their handler threads)
    close(ListeningSocket);
    
    // Log completion of server shutdown
    g_serverState.logEvent("SERVER", "Server shutdown complete");
}

// ============================================================================
// STANDALONE SERVER ENTRY POINT
// ============================================================================

/// @brief Main entry point when compiling standalone server (not as part of GUI)
/// Only compiled when both UNIT_TEST and GUI_BUILD are not defined
/// Starts the server main loop
#if !defined(UNIT_TEST) && !defined(GUI_BUILD)
int main()
{
    // Run the server main loop (blocking until shutdown)
    server_run_loop();
    return 0;
}
#endif
