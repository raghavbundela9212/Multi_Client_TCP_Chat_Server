/*
 * client.c — Beginner-friendly TCP chat client
 * ================================================
 *
 * WHAT THIS PROGRAM DOES
 * -----------------------
 * Connects to server.c, then lets you type messages (sent to everyone
 * else in the chat room) while simultaneously showing messages typed by
 * other clients — all without threads.
 *
 * HOW? We use select() again, but this time to watch TWO file
 * descriptors at once:
 *   - fd 0 (STDIN, your keyboard)
 *   - the socket connected to the server
 * Whichever one has data ready, we read from it and act accordingly.
 * This is the same trick servers use, just applied on the client side.
 *
 * THE CORE SOCKET LIFECYCLE (client side)
 * ------------------------------------------
 *   socket()  -> create an endpoint
 *   connect() -> actively reach out to the server's address:port
 *   send()/recv() -> talk
 *   close()   -> hang up
 *
 * HOW TO RUN
 * ------------
 *   ./client 127.0.0.1 5000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    // ---- 1. socket(): create the client-side endpoint ----------------
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert the human-readable IP string ("127.0.0.1") into the
    // binary form sockets need.
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip);
        close(sock_fd);
        return 1;
    }

    // ---- 2. connect(): perform the TCP three-way handshake -----------
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("Connected to %s:%d. Type a message and press Enter.\n", server_ip, port);
    printf("Type /quit to disconnect.\n\n");

    fd_set read_fds;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);   // fd 0 = your keyboard
        FD_SET(sock_fd, &read_fds);        // the server socket

        int max_fd = (sock_fd > STDIN_FILENO) ? sock_fd : STDIN_FILENO;

        // Wait until EITHER you type something OR the server sends data.
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // ---- Data arrived from the server: print it ----------------
        if (FD_ISSET(sock_fd, &read_fds)) {
            char buffer[BUFFER_SIZE];
            int bytes_read = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_read <= 0) {
                printf("\nServer closed the connection.\n");
                break;
            }
            buffer[bytes_read] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        // ---- You typed something: send it ----------------------------
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char line[BUFFER_SIZE];
            if (fgets(line, sizeof(line), stdin) == NULL) {
                break; // e.g. Ctrl+D
            }

            if (strncmp(line, "/quit", 5) == 0) {
                break;
            }

            send(sock_fd, line, strlen(line), 0);
        }
    }

    close(sock_fd);
    printf("Disconnected.\n");
    return 0;
}
