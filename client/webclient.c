#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define DELIMITER "\r\n\r\n"
#define DELIMITER_LENGTH 4

void process_commands(int sockfd);
void handle_get_request(int sockfd, const char *path);
void handle_post_request(int sockfd, const char *path);
int connect_to_server(const char *host, int port);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s server_ip port_number\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port_number = atoi(argv[2]);
    int sockfd = connect_to_server(server_ip, port_number);

    if (sockfd < 0) {
        fprintf(stderr, "Failed to connect to the server\n");
        return EXIT_FAILURE;
    }

    process_commands(sockfd);

    close(sockfd);
    return EXIT_SUCCESS;
}

void process_commands(int sockfd) {
    FILE *file = fopen("input.txt", "r");
    char line[BUFFER_SIZE];

    if (!file) {
        perror("Error opening file");
        return;
    }

    // Read and process each line from the file
    while (fgets(line, BUFFER_SIZE, file)) {
        char method[15], path[BUFFER_SIZE];
        if (sscanf(line, "%s %s", method, path) != 2) {
            fprintf(stderr, "Invalid command format\n");
            continue;
        }

        if (strcmp(method, "client_get") == 0) {
            handle_get_request(sockfd, path);
        } else if (strcmp(method, "client_post") == 0) {
            handle_post_request(sockfd, path);
        } else {
            fprintf(stderr, "Unsupported method\n");
        }
    }

    fclose(file);
}


void handle_get_request(int sockfd, const char *path) {
    char buffer[BUFFER_SIZE];
    char host[] = "localhost";
    snprintf(buffer, sizeof(buffer), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);

    // Send the GET request
    int n = send(sockfd, buffer, strlen(buffer), 0);
    
    if (n < 0) {
        perror("Error writing to socket");
        return;
    }

    // Read the response from the server
    char filePath[1024] = "files";
    strcat(filePath, path);
    FILE* file = fopen(filePath, "wb"); 
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    
    char* body_start = NULL;
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer
        ssize_t bytes_read = read(sockfd, buffer, BUFFER_SIZE - 1);

        if (bytes_read <= 0) {
            break;
        }
        body_start = strstr(buffer, DELIMITER);
        // Print the data received
        printf("%s", buffer);
        if (body_start != NULL) {
            body_start += DELIMITER_LENGTH;
            fwrite(body_start, bytes_read - (body_start - buffer), 1, file);
        }
    }
    fclose(file);
}

void handle_post_request(int sockfd, const char *path) {
    char header[BUFFER_SIZE];
    char host[] = "localhost";
    char content_type[] = "application/x-www-form-urlencoded";
    char filePath[1024] = "files";
    strcat(filePath, path);
    printf("%s\n", filePath);
    FILE* file = fopen(filePath, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *body = calloc(fsize + 1, sizeof(char));
    fread(body, 1, fsize, file);
    fclose(file);

    // Construct the POST request header
    int content_length = fsize;
    snprintf(header, BUFFER_SIZE,
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "\r\n"
             "%s",
             path, host, content_type, content_length, body);
    // Send the POST request
    int n = send(sockfd, header, strlen(header), 0);
    if (n < 0) {
        perror("Error writing to socket");
    }
    char buffer[BUFFER_SIZE];
    while (1) {
        ssize_t bytes_read = read(sockfd, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            break;
        }
        // Print the data received
        printf("%s", buffer);
    }
}

int connect_to_server(const char *host, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Cannot create socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    return sockfd;
}
