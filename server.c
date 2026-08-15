/*
 * server.c — A beginner-friendly multi-client TCP chat server
 * ==============================================================
 *
 * WHAT THIS PROGRAM DOES
 * -----------------------
 * It opens a TCP socket, listens for incoming client connections, and lets
 * every connected client broadcast text messages to every OTHER connected
 * client — like a very small group chat room.
 *
 * WHY select() INSTEAD OF THREADS?
 * ---------------------------------
 * The simplest way to "handle many clients at once" is to spawn a new
 * thread (or process) per client. That works, but it hides what is really
 * happening at the socket level behind OS scheduling.
 *
 * select() is the classic, single-threaded way to watch MANY file
 * descriptors (sockets) at once and ask the kernel: "wake me up when ANY
 * of these is ready to be read." This program uses ONE thread, ONE loop,
 * and select() to multiplex all client connections. It's a great next
 * step after writing a single-client server, and a great foundation for
 * later learning epoll/kqueue/libuv/io_uring.
 *
 * THE CORE SOCKET LIFECYCLE (server side)
 * -----------------------------------------
 *   socket()   -> create an endpoint (like getting a phone handset)
 *   bind()     -> attach it to an address:port (like getting a phone number)
 *   listen()   -> mark it as ready to accept incoming calls
 *   accept()   -> answer an incoming call, returns a NEW socket just for
 *                 that caller (the original socket keeps listening for more)
 *   recv()/send() -> talk over the call
 *   close()    -> hang up
 *
 * HOW TO BUILD & RUN
 * --------------------
 *   make
 *   ./server 5000
 *
 * Then in other terminals:
 *   ./client 127.0.0.1 5000
 *
 * Type messages in any client window and press Enter — every other
 * connected client will see them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>      // va_list (for our small logging helper)
#include <unistd.h>      // close(), read(), write()
#include <arpa/inet.h>   // sockaddr_in, inet_ntoa, htons
#include <sys/select.h>  // select(), fd_set

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024

// One slot per possible client. fd == 0 means the slot is empty/unused.
typedef struct {
    int fd;
    struct sockaddr_in addr;
} client_t;

client_t clients[MAX_CLIENTS];

// Print a message to the server's own console with a timestamp-free prefix.
static void log_msg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

// Send `message` to every connected client except the one whose socket
// descriptor is `sender_fd` (pass -1 to send to everyone, e.g. server
// announcements).
static void broadcast(const char *message, int sender_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != 0 && clients[i].fd != sender_fd) {
            // send() can theoretically write fewer bytes than requested on
            // a stream socket; for short chat lines this rarely matters,
            // but production code should loop until everything is sent.
            send(clients[i].fd, message, strlen(message), 0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    // ---- 1. socket(): create the listening socket -------------------
    // AF_INET      = IPv4
    // SOCK_STREAM  = TCP (reliable, ordered, byte-stream)
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    // Allow the OS to immediately reuse this port if we restart the
    // server (otherwise you'll hit "Address already in use" for ~60s).
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // ---- 2. bind(): attach the socket to an address:port ------------
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;   // listen on all local interfaces
    server_addr.sin_port = htons(port);         // host-to-network byte order

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    // ---- 3. listen(): mark the socket as accepting connections ------
    // The backlog (here, 10) is the max number of pending connections
    // the kernel will queue before you call accept() on them.
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    log_msg("Chat server listening on port %d ...", port);

    memset(clients, 0, sizeof(clients));

    fd_set read_fds;
    int max_fd;

    // ---- Main event loop ---------------------------------------------
    while (1) {
        // select() destroys the fd_set it's given, so we rebuild it
        // fresh from our client list every single iteration.
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        max_fd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0) {
                FD_SET(clients[i].fd, &read_fds);
                if (clients[i].fd > max_fd) {
                    max_fd = clients[i].fd;
                }
            }
        }

        // Block here until at least one socket has data waiting, or a
        // new connection is incoming on listen_fd. NULL timeout = wait
        // forever.
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }

        // ---- New incoming connection? ----------------------------
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);

            // ---- 4. accept(): complete the handshake, get a new fd --
            int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_fd < 0) {
                perror("accept");
            } else {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == 0) {
                        slot = i;
                        break;
                    }
                }

                if (slot == -1) {
                    log_msg("Server full, rejecting a new connection.");
                    const char *full_msg = "Server is full. Try again later.\n";
                    send(new_fd, full_msg, strlen(full_msg), 0);
                    close(new_fd);
                } else {
                    clients[slot].fd = new_fd;
                    clients[slot].addr = client_addr;
                    log_msg("New connection: %s:%d (slot %d)",
                            inet_ntoa(client_addr.sin_addr),
                            ntohs(client_addr.sin_port), slot);

                    char welcome[BUFFER_SIZE];
                    snprintf(welcome, sizeof(welcome),
                             "*** %s:%d joined the chat ***\n",
                             inet_ntoa(client_addr.sin_addr),
                             ntohs(client_addr.sin_port));
                    broadcast(welcome, new_fd);
                }
            }
        }

        // ---- Existing clients: check who sent data -----------------
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i].fd;
            if (fd > 0 && FD_ISSET(fd, &read_fds)) {
                char buffer[BUFFER_SIZE];
                // ---- 5. recv(): read bytes from this client ---------
                int bytes_read = recv(fd, buffer, sizeof(buffer) - 1, 0);

                if (bytes_read <= 0) {
                    // 0 = client closed the connection gracefully.
                    // negative = an error occurred.
                    log_msg("Client %s:%d disconnected (slot %d)",
                            inet_ntoa(clients[i].addr.sin_addr),
                            ntohs(clients[i].addr.sin_port), i);

                    char bye_msg[BUFFER_SIZE];
                    snprintf(bye_msg, sizeof(bye_msg),
                             "*** %s:%d left the chat ***\n",
                             inet_ntoa(clients[i].addr.sin_addr),
                             ntohs(clients[i].addr.sin_port));

                    close(fd);
                    clients[i].fd = 0;
                    broadcast(bye_msg, -1);
                } else {
                    buffer[bytes_read] = '\0';

                    char out[BUFFER_SIZE + 64];
                    snprintf(out, sizeof(out), "[%s:%d] %s",
                             inet_ntoa(clients[i].addr.sin_addr),
                             ntohs(clients[i].addr.sin_port), buffer);

                    log_msg("%s", out);
                    broadcast(out, fd);
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}
