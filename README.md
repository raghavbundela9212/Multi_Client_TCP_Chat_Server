# c-tcp-chat — A Multi-Client TCP Chat Server in C

A small, from-scratch chat room written in pure C using **Berkeley sockets**
and **`select()`** — no external libraries, no frameworks. Built to
demonstrate how network programs actually work under the hood: creating
sockets, binding addresses, accepting connections, and multiplexing many
clients on a single thread.

```
        ┌──────────┐        connect()         ┌──────────┐
        │ client A │ ───────────────────────▶ │          │
        └──────────┘                           │          │
        ┌──────────┐        connect()          │  server  │  broadcasts every
        │ client B │ ───────────────────────▶ │ (select) │  message to all
        └──────────┘                           │          │  OTHER clients
        ┌──────────┐        connect()          │          │
        │ client C │ ───────────────────────▶ │          │
        └──────────┘                           └──────────┘
```

## Why this project

Most "networking in C" tutorials stop at a single client talking to a
single server. This one goes one step further and handles **multiple
simultaneous clients without spawning a thread or process per client** —
using `select()` to watch many sockets at once. It's a natural bridge
between "I understand what a socket is" and "I understand how real
event-driven servers (nginx, Redis, Node.js) are built."

## Concepts demonstrated

- The TCP socket lifecycle: `socket()` → `bind()` → `listen()` → `accept()`
  (server) and `socket()` → `connect()` (client)
- Reading/writing a byte stream with `send()` / `recv()`
- Network byte order (`htons`) and address conversion (`inet_pton`, `inet_ntoa`)
- I/O multiplexing with `select()` and `fd_set` — one thread watching many
  file descriptors at once
- Basic connection-state bookkeeping (tracking active clients in an array)
- Graceful handling of client disconnects (`recv()` returning 0)

## Project structure

```
c-tcp-chat/
├── server.c    # Multi-client chat server (select()-based)
├── client.c    # Interactive chat client (also select()-based)
├── Makefile
└── README.md
```

## Build

Requires `gcc` and a POSIX system (Linux, macOS, WSL).

```bash
make
```

This produces two binaries: `server` and `client`.

## Run

**Terminal 1 — start the server on a port of your choice:**
```bash
./server 5000
```

**Terminal 2, 3, 4... — connect one or more clients:**
```bash
./client 127.0.0.1 5000
```

Type a message and press Enter to broadcast it to everyone else in the
chat. Type `/quit` to disconnect. Open a couple of terminal windows side
by side to see the broadcast in action.

## How it works (short version)

**Server:** keeps one "listening" socket and an array of connected client
sockets. On every loop iteration it rebuilds an `fd_set` containing the
listening socket plus every connected client, then calls `select()` and
blocks until the kernel says *something* is ready. If the listening
socket is ready, a new client is trying to connect, so it calls
`accept()`. If a client socket is ready, that client sent data, so it
calls `recv()` and forwards the message to every other client.

**Client:** watches two file descriptors at once — standard input (your
keyboard) and the socket to the server — using the same `select()` trick.
Whichever one has data first gets handled: keyboard input is sent to the
server, and data from the server is printed to your screen. This is what
lets you type and receive messages "at the same time" without threads.

## Known limitations (intentional, for a learning project)

- Fixed-size client table (`MAX_CLIENTS`, currently 30) rather than a
  dynamic data structure
- No authentication, encryption (no TLS), or message framing beyond
  newline-delimited text
- `send()`/`recv()` aren't looped to guarantee all bytes are transferred
  on partial writes/reads — fine for short chat lines, not for production

These are natural "next steps" if you want to extend the project further
(see below).

## Possible extensions

- Swap `select()` for `poll()` or `epoll()` (Linux) and compare
- Store clients in a linked list or hash table (keyed by username) instead
  of a fixed array
- Add `/nick <name>` command and per-client usernames
- Add simple message framing (length-prefixed) instead of raw newlines
- Wrap the socket in TLS using OpenSSL

## License

MIT — free to use, fork, and extend for learning or portfolio purposes.
