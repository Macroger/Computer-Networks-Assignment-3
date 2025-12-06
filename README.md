# Message Board — Server (Assignment 3)

## Overview

This project implements a TCP-based message-board server with a real-time GUI dashboard. The server accepts requests from clients and responds with requested resources (posts). Clients can:

- POST one or multiple messages to the board
- GET the entire board
- GET posts filtered by `Author` and/or `Title`
- QUIT the session

The server runs in a background thread while a real-time FTXUI-based GUI dashboard displays live statistics, message board activity, event logs, and connected clients.

## Protocol Summary

- Transport: TCP
- Default port: `26500`
- Message framing: custom delimiters + explicit end-of-message marker

Delimiter constants used in this project (as discussed by the team):

- Field delimiter: `}+{`
- End-of-message marker: `}}&{{`
- POST separator (multiple POSTS in one request): `}#{`

Note: All of the delimiters above are literal text sequences to be parsed by the server.

### High-level Commands (Client -> Server)

- `POST` — Submit a new post or multiple posts in a single request.
- `GET_BOARD` — Request the entire message board. Optional filters: `Author` and/or `Title`. If both filters are empty, server returns the whole board.
- `QUIT` — Client ends communication (initiate graceful shutdown for the connection).

### Server Response States

- `POST_OK` — Server confirms the POST succeeded.
- `POST_ERROR` — Server signals an error processing the POST.
- `SERVER SHUTDOWN` — Server is shutting down gracefully.

## Message Formats and Examples

Single POST example (one message):

```
POST}+{Matt Schatz}+{First post}+{YAY my first post}}&{{
```

Multiple messages (single request) example:

```
POST}+{Matt Schatz}+{First post}+{YAY my first post}#{Matt Schatz}+{My second post!}+{Heres the contents of my second posting!}}&{{
```

Notes:
- The example POST command begins with the literal `POST` command, followed by one or more posts separated by `}#{` (when multiple posts are present), and the entire request is terminated by the `}}&{{` end-of-message marker.
- Each post is encoded as `Author}+{Title}+{Message` (with the `}+{` field delimiter between them).

GET request examples (conceptual):

- Get entire board (no filters):
```
GET_BOARD}+{ }+{ }}}&{{
```
- Get posts by author only:
```
GET_BOARD}+{Matt Schatz}+{ }}}&{{
```
- Get posts by title only:
```
GET_BOARD}+{ }+{First post}}&{{
```
(Actual wire-format for GET is simple text using same delimiters; implementation may accept empty fields as shown.)

## Server Behavior

- The server listens on the configured TCP port and accepts incoming connections.
- For each incoming request it reads until it receives the `}}&{{` marker, then parses the request using the `}+{` and `}#{` delimiters.
- For `POST` commands the server will attempt to parse each post into `(Author, Title, Message)` tuples and store them in the in-memory board. On success, it returns `POST_OK`; on parse or storage error it returns `POST_ERROR`.
- For `GET_BOARD` the server filters stored posts by `Author` and/or `Title` when provided; if both filters are empty, it returns the whole board.
- For `QUIT` the server ends the session for that client connection.

## Build & Run

Build the integrated server + GUI binary:

```bash
cd /home/macrog/code/School/computer_networks_assignment_3
./build.sh gui
```

Run the server with GUI:

```bash
./build/server_gui
```

The server will start listening on port 26500 in the background, and the GUI dashboard will display in your terminal.

## GUI Features

### Tabbed Interface
- **Message Board**: Displays all posted messages with pagination (7 messages per page). Shows newest messages first. Includes filtering by title and author with "Apply Filters" and "Clear Filters" buttons.
- **Event Log**: Real-time event tracking (connections, disconnections, posts, errors) with 7 events per page.
- **Connected Clients**: Lists all currently connected clients with their IDs.
- **Stats**: Displays server statistics (active connections, total messages, messages received).

### Smart Navigation
- **Pagination**: Browse messages and events page by page with Previous/Next buttons
- **Jump to Latest**: Instantly jump to the first page when new messages arrive
- **New Message Banner**: Yellow notification banner appears when new content arrives while viewing older pages
- **Colored Tabs**: Magenta (Board), Cyan (Log), Yellow (Clients), Blue (Stats)

### Message Board Features
- Real-time updates as clients post messages
- Newest messages shown first (reverse chronological order)
- Sequential numbering (#1, #2, etc.) for easy reference
- Message display shows: Author, Title, Message, Client ID
- Title and Author filtering with Apply/Clear buttons
- Page indicator showing current page/total pages

### Event Log Features
- Color-coded events: Green (CONNECT), Red (DISCONNECT), Yellow (POST), Cyan (GET_BOARD), Red (ERROR)
- Timestamp for each event
- Raw wire-format message display for debugging
- Same pagination and Jump to Latest functionality as Message Board

### Server Control
- **Add Test Posts**: Generates 5 random test posts instantly (clientId=999) for testing
- **Shutdown Server**: Gracefully shuts down the server, sends goodbye message to all connected clients, and closes the application

## Testing

Run the unit test suite:

```bash
./build.sh tests
```

All 31 unit tests should pass, covering:
- Protocol parsing (GET_BOARD, POST, QUIT, INVALID_COMMAND)
- Multiple client handling
- Thread-safe operations
- Message filtering
- Error conditions 
---