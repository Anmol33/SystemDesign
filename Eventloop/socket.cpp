#include <iostream>     // For standard I/O operations (cout, cerr)
#include <sys/socket.h> // For socket, bind, listen, accept, read, write
#include <netinet/in.h> // For sockaddr_in, INADDR_ANY
#include <arpa/inet.h>  // For inet_ntoa
#include <unistd.h>     // For close
#include <fcntl.h>      // For fcntl (setting non-blocking mode)
#include <sys/epoll.h>  // For epoll_create1, epoll_ctl, epoll_wait
#include <vector>       // For std::vector
#include <cstring>      // For memset

// Define constants for the server
const int PORT = 8080;
const int MAX_EVENTS = 10; // Maximum number of events to be returned by epoll_wait
const int BUFFER_SIZE = 1024; // Size of the buffer for reading/writing data

/**
 * @brief Sets a file descriptor (socket) to non-blocking mode.
 * @param fd The file descriptor to set.
 * @return 0 on success, -1 on failure.
 */
int set_nonblocking(int fd) {
    // Get the current flags of the file descriptor
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL"); // Print error if getting flags fails
        return -1;
    }
    // Add the O_NONBLOCK flag to the existing flags
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK"); // Print error if setting flags fails
        return -1;
    }
    return 0; // Success
}

int main() {
    // 1. Create a socket for the server to listen on
    // AF_INET: IPv4 protocol
    // SOCK_STREAM: TCP (stream-oriented) socket
    // 0: Default protocol for SOCK_STREAM (TCP)
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket"); // Print error if socket creation fails
        return 1;
    }

    // Set SO_REUSEADDR option to allow reusing the address immediately after closing
    // This helps in quickly restarting the server after a crash or shutdown
    int optval = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(listen_sock);
        return 1;
    }

    // 2. Bind the socket to a specific IP address and port
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Clear the structure
    server_addr.sin_family = AF_INET;             // IPv4
    server_addr.sin_port = htons(PORT);           // Port number in network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;     // Listen on all available network interfaces

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind"); // Print error if binding fails
        close(listen_sock);
        return 1;
    }

    // 3. Start listening for incoming connections
    // 10: Maximum number of pending connections in the queue
    if (listen(listen_sock, 10) == -1) {
        perror("listen"); // Print error if listen fails
        close(listen_sock);
        return 1;
    }

    // Set the listening socket to non-blocking mode
    if (set_nonblocking(listen_sock) == -1) {
        close(listen_sock);
        return 1;
    }

    std::cout << "Server listening on port " << PORT << std::endl;

    // 4. Create an epoll instance
    // epoll_create1(0) is preferred over epoll_create() as it allows flags
    // (though 0 means no flags for now).
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1"); // Print error if epoll instance creation fails
        close(listen_sock);
        return 1;
    }

    // 5. Add the listening socket to the epoll instance
    // EPOLLIN: Event for data available to read (new connection for listening socket)
    // EPOLLET: Edge-triggered mode (only report events when state changes, e.g., data arrives)
    //          Requires careful handling: read all available data, write until EAGAIN/EWOULDBLOCK.
    //          For simplicity, we'll use level-triggered (default) for now, but edge-triggered is more performant.
    // EPOLL_CTL_ADD: Add a file descriptor to the epoll instance
    epoll_event event;
    event.events = EPOLLIN; // Listen for incoming data/connections
    event.data.fd = listen_sock; // Associate the event with the listening socket
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
        perror("epoll_ctl: listen_sock"); // Print error if adding listening socket fails
        close(listen_sock);
        close(epoll_fd);
        return 1;
    }

    // Vector to store events returned by epoll_wait
    std::vector<epoll_event> events(MAX_EVENTS);
    char buffer[BUFFER_SIZE]; // Buffer for reading/writing data

    // 6. The Event Loop (main processing loop)
    while (true) {
        // epoll_wait: Waits for events on the registered file descriptors
        // epoll_fd: The epoll instance file descriptor
        // events.data(): Pointer to the array where events will be stored
        // MAX_EVENTS: Maximum number of events to retrieve
        // -1: Timeout (wait indefinitely until an event occurs)
        int num_events = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
        if (num_events == -1) {
            perror("epoll_wait"); // Print error if epoll_wait fails
            close(listen_sock);
            close(epoll_fd);
            return 1;
        }

        // Iterate through the ready events
        for (int i = 0; i < num_events; ++i) {
            int current_fd = events[i].data.fd; // File descriptor associated with the event

            // Case 1: New connection on the listening socket
            if (current_fd == listen_sock) {
                sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                // Accept the new connection
                int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
                if (client_sock == -1) {
                    perror("accept"); // Print error if accept fails
                    continue; // Continue to the next event
                }

                // Set the new client socket to non-blocking mode
                if (set_nonblocking(client_sock) == -1) {
                    close(client_sock); // Close the client socket if setting non-blocking fails
                    continue;
                }

                // Add the new client socket to the epoll instance
                // EPOLLIN: Listen for incoming data from the client
                // EPOLLRDHUP: Detect when the peer closes its half of the connection
                event.events = EPOLLIN | EPOLLRDHUP;
                event.data.fd = client_sock;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &event) == -1) {
                    perror("epoll_ctl: client_sock"); // Print error if adding client socket fails
                    close(client_sock);
                    continue;
                }
                std::cout << "New connection from " << inet_ntoa(client_addr.sin_addr)
                          << ":" << ntohs(client_addr.sin_port) << " on FD " << client_sock << std::endl;
            }
            // Case 2: Data available on an existing client socket
            else if (events[i].events & EPOLLIN) {
                // Read data from the client socket
                ssize_t bytes_read = read(current_fd, buffer, sizeof(buffer));
                if (bytes_read == -1) {
                    perror("read"); // Print error if read fails
                    // In a real application, handle specific errors like EAGAIN/EWOULDBLOCK for edge-triggered
                    close(current_fd); // Close the socket on read error
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL); // Remove from epoll
                    std::cout << "Client on FD " << current_fd << " closed due to read error." << std::endl;
                } else if (bytes_read == 0) {
                    // Client closed the connection (EOF)
                    std::cout << "Client on FD " << current_fd << " disconnected." << std::endl;
                    close(current_fd); // Close the socket
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL); // Remove from epoll
                } else {
                    // Data received, echo it back to the client
                    std::cout << "Received " << bytes_read << " bytes from FD " << current_fd << ": ";
                    // Print the received data (ensure it's null-terminated for string printing)
                    buffer[bytes_read] = '\0'; // Null-terminate for printing
                    std::cout << buffer << std::endl;

                    // Write the received data back to the client
                    ssize_t bytes_written = write(current_fd, buffer, bytes_read);
                    if (bytes_written == -1) {
                        perror("write"); // Print error if write fails
                        close(current_fd); // Close the socket on write error
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL); // Remove from epoll
                        std::cout << "Client on FD " << current_fd << " closed due to write error." << std::endl;
                    } else if (bytes_written < bytes_read) {
                        // This case is important for non-blocking writes:
                        // Not all data was written. In a real app, you'd buffer the remaining data
                        // and register for EPOLLOUT to write when the socket is writable again.
                        std::cerr << "Warning: Only " << bytes_written << " of " << bytes_read
                                  << " bytes written to FD " << current_fd << std::endl;
                    }
                }
            }
            // Case 3: Client disconnected (EPOLLRDHUP or EPOLLHUP)
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) {
                std::cout << "Client on FD " << current_fd << " disconnected (EPOLLRDHUP/HUP)." << std::endl;
                close(current_fd); // Close the socket
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL); // Remove from epoll
            }
            // Case 4: Other errors or unexpected events
            else {
                std::cerr << "Unexpected event " << events[i].events << " on FD " << current_fd << std::endl;
                close(current_fd); // Close the socket on unexpected event
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL); // Remove from epoll
            }
        }
    }

    // Clean up (this part is usually unreachable in a simple infinite loop server)
    close(listen_sock);
    close(epoll_fd);

    return 0;
}
