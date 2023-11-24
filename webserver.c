#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define DEFAULT_PORT 8080

void *connection_handler(void *socket){
    int client_socket = *(int*)socket;
    free(socket); // Free memory allocated for socket descriptor
    char buffer[1024] = {0}; // Buffer for incoming messages
    char *hello = "Hello from server"; // Message to send to client

    // Read message from client
    int valread = read(client_socket, buffer, 1024 - 1);
    if (valread < 0) {
        perror("read");
        exit(EXIT_FAILURE);
    }
    buffer[valread] = '\0'; // Null terminate string
    printf("Message from client: %s\n", buffer);

    // Print message from client
    char method[10], path[1024], protocol[10];
    sscanf(buffer, "%s %s %s", method, path, protocol);

    printf("Path: %s\n", path);
    // printf("Protocol: %s\n", protocol);

    if (strcmp(method, "GET") == 0) {

        FILE *file = fopen(path, "r");
        // FILE *file = fopen("file.txt", "r");
        if (file == NULL) {
            const char *notFoundMessage = "HTTP/1.1 404 Not Found\r\n\r\n";
            send(client_socket, notFoundMessage, strlen(notFoundMessage), 0);
        } else {
            // Read file content
            fseek(file, 0, SEEK_END);
            long fsize = ftell(file);
            fseek(file, 0, SEEK_SET);
            char *data = malloc(fsize + 1);
            fread(data, 1, fsize, file);
            fclose(file);
            data[fsize] = 0;

            // Send file content to client
            char header[1024];
            sprintf(header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", fsize);
            send(client_socket, header, strlen(header), 0);
            send(client_socket, data, fsize, 0);
            free(data);
        }
    } else if (strcmp(method, "POST") == 0) {
        const char *okMessage = "HTTP/1.1 200 OK\r\n\r\n";
        send(client_socket, okMessage, strlen(okMessage), 0);

        char fileBuffer[2048];  // Buffer for file content
        int fileValRead = read(client_socket, fileBuffer, 2048 - 1);
        if (fileValRead < 0) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        fileBuffer[fileValRead] = '\0'; // Null terminate string
        printf("File content: %s\n", fileBuffer);

    } else {
        const char *notImplementedMessage = "HTTP/1.1 501 Not Implemented\r\n\r\n";
        send(client_socket, notImplementedMessage, strlen(notImplementedMessage), 0);
    }

    // Source: After finishing the transmission, the server should keep the connection open waiting for new requests from the same client.
    // Source: Close if connection timed out
    return NULL;
}

int main(int argc, char const* argv[]){    
    int server_fd;    // File descriptors for server and client sockets
    struct sockaddr_in address;   // Address structure for IPv4
    int opt = 1;                  // Option value for setsockopt
    int addrlen = sizeof(address);
    char buffer[1024] = {0};      // Buffer for incoming messages
    char *hello = "Hello from server"; // Message to send to client

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Attach socket to port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;           // Address family: IPv4
    address.sin_addr.s_addr = INADDR_ANY;   // Accept any incoming messages
    address.sin_port = htons(DEFAULT_PORT); // Convert to network byte order
    
    // Bind socket to port 8080
    if (bind(server_fd, (struct sockaddr*) &address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    while(1) {
        // Accept incoming connection
        int *new_socket = malloc(sizeof(int));
        if ((*new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            free(new_socket);
            continue;
        }

        // Create thread to handle connection
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, connection_handler, (void*)new_socket) < 0) {
            perror("pthread_create");
            free(new_socket);
            continue;
        }
    }

    close(server_fd);

    return 0;   
}  