# Message Board — Server (Assignment 3)

## Overview

This project implements a simple TCP-based message-board service (server + client). The server accepts requests from clients and responds with requested resources (posts). Clients can:

- POST one or multiple messages to the board
- GET the entire board
- GET posts filtered by `Author` and/or `Title`
- QUIT the session

Each post/message is represented as a single string that contains three component fields: `Author`, `Title`, and `Message`. Fields are separated using a custom delimiter. The server parses incoming messages, stores posts in memory (and optionally persists them later), and returns responses over TCP.

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

Responses may be plain text strings (simple) or a small structured response format — the chosen implementation is kept intentionally simple for the assignment.

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

## Build & Run (example)

Build the current `server.cpp` with `g++` (C++17):

```bash
g++ -std=c++17 -O2 server.cpp -o server
```

Run the server (default port `26500`):

```bash
./server
```

You can test with `netcat` (`nc`) or `telnet`. Use `printf` or `echo -n` to send the literal delimiters and termination marker. Example sending a single POST with `nc`:

```bash
printf 'POST}+{Matt Schatz}+{First post}+{YAY my first post}}&{{' | nc localhost 26500
```

Example sending multiple posts in one request:

```bash
printf 'POST}+{Matt Schatz}+{First post}+{YAY my first post}#{Matt Schatz}+{My second post!}+{Heres the contents of my second posting!}}&{{' | nc localhost 26500
```

Example GET for the whole board:

```bash
printf 'GET_BOARD}+{ }+{ }}}&{{' | nc localhost 26500
```


## Parsing rules (implementation notes)

- Read incoming bytes until the exact sequence `}}&{{` appears; treat the preceding content as the full message body.
- Split the body by `}#{` to get individual post entries (only for `POST` requests). If not `POST`, skip this step.
- For each post entry, split by `}+{` into exactly three parts: `Author`, `Title`, `Message`. Trim whitespace if desired.
- If splitting fails (not enough parts) the server should treat that as a `POST_ERROR` and respond accordingly.

## Storage

- Current design stores posts in-memory in a vector/list of small structs like `(id, author, title, message, timestamp)`.
- If persistence is required, an append-only file (one JSON-line per post) or an embedded DB like `sqlite3` can be added.

## Concurrency and Scaling (future)

- Assignment baseline: single-connection handling is acceptable.
- Extra credit: accept multiple clients using either a thread-per-connection model with `std::thread` or an event-driven `select`/`poll`/`epoll` loop.
- For concurrent writes, protect shared in-memory structures with `std::mutex`.

## Next Steps / TODOs

- [x] Decide final port: `26500`
- Add unit tests for parsing and handler logic.
- Add persistence (file or sqlite)

## Contacts
 - Team Members:
    - Matthew G. Schatz (as in `server.cpp`).
    - Kian Cloutier 
---