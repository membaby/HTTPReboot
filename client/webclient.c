#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024
#define DELIMITER "\r\n\r\n"
#define DELIMITER_LENGTH 4

void process_commands(int sockfd);
void handle_get_request(int sockfd, const char *path);
void handle_post_request(int sockfd, const char *path);
int connect_to_server(const char* restrict domain, const char* restrict port);
char* getContentLength(int sockfd, int* remainingBodyLength);

int main(int argc, char *argv[]) {
    // Check the number of arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s server_ip port_number\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Connect to the server (Open a TCP connection)
    int sockfd = connect_to_server(argv[1], argv[2]);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to connect to the server\n");
        return EXIT_FAILURE;
    }

    // Process commands from the user
    process_commands(sockfd);

    // Close the socket
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

        printf("\n\nSending Request: Method: %s, Path: %s\n", method, path);

        if (strncmp(method, "client_get", 10) == 0) {
            handle_get_request(sockfd, path);
        } else if (strncmp(method, "client_post", 11) == 0) {
            handle_post_request(sockfd, path);
        } else {
            fprintf(stderr, "Unsupported method\n");
        }
    }

    fclose(file);
}

char* getContentLength(int sockfd, int* remainingBodyLength) {
    char headerBuffer[BUFFER_SIZE] = {0};
    char* endOfHeaders;
    int totalBytesRead = 0;
    int contentLength = 0;
    do {
        ssize_t bytesRead = read(sockfd, headerBuffer + totalBytesRead, 1);  // Read one byte at a time
        if (bytesRead <= 0) {
            perror("Error reading from socket");
            return "0";
        }
        totalBytesRead += bytesRead;
        headerBuffer[totalBytesRead] = '\0';  // Null-terminate the string
        endOfHeaders = strstr(headerBuffer, "\r\n\r\n");  // Look for end of headers
    } while (endOfHeaders == NULL);

    printf("%s", headerBuffer);

    // Parse the content length
    char *contentLengthStr = strstr(headerBuffer, "Content-Length:");
    if (contentLengthStr) {
        sscanf(contentLengthStr, "Content-Length: %d", &contentLength);
    } else {
        contentLengthStr = "0";
    }

    int bodyLength = totalBytesRead - (endOfHeaders + 4 - headerBuffer);  // Initial body length read along with headers
    char *bodyStart = endOfHeaders + 4;  // Body starts after "\r\n\r\n"
    *remainingBodyLength = contentLength - bodyLength;

    return contentLengthStr;
}


void handle_get_request(int sockfd, const char *path) {
    char buffer[BUFFER_SIZE];
    char host[] = "localhost";
    snprintf(buffer, sizeof(buffer), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n", path, host);

    // Send the GET request
    int n = send(sockfd, buffer, strlen(buffer), 0);
    
    if (n < 0) {
        perror("Error writing to socket");
        return;
    }

    int remainingBodyLength = 0;
    char* contentLengthStr = getContentLength(sockfd, &remainingBodyLength);
    if (remainingBodyLength <= 0) return;
    
    char filePath[1024] = "files";
    strcat(filePath, path);
    FILE* file = fopen(filePath, "wb"); 
    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    // Read remaining part of the body
    while (remainingBodyLength > 0) {
        ssize_t bytesRead = read(sockfd, buffer, (BUFFER_SIZE - 1 < remainingBodyLength) ? BUFFER_SIZE - 1 : remainingBodyLength);

        if (bytesRead <= 0) {
            perror("Error reading from socket");
            return;
        }
        buffer[bytesRead] = '\0';  // Null-terminate the string
        remainingBodyLength -= bytesRead;

        fwrite(buffer, bytesRead, 1, file);
        printf("%s", buffer);
    }

    fclose(file);
}

void handle_post_request(int sockfd, const char *path) {
    char buffer[BUFFER_SIZE];
    char host[] = "localhost";
    char content_type[] = "application/x-www-form-urlencoded";
    char filePath[1024];
    snprintf(filePath, sizeof(filePath), "files%s", path);
    printf("%s\n", filePath);
    FILE* file = fopen(filePath, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        perror("Error opening file");
        return;
    }
    snprintf(buffer, BUFFER_SIZE,
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "\r\n",
             path, host, content_type, (long)st.st_size);
    if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
        perror("Error writing to socket");
        return;
    }
    size_t total_sent = 0;
    int bytes_read = 0;
    while(total_sent < st.st_size){
        bytes_read = fread(buffer, 1, BUFFER_SIZE-1, file);
        if(bytes_read == 0)
            break;
        if (send(sockfd, buffer, bytes_read, 0) < 0) {
            perror("Error writing to socket");
            return;
        }
        total_sent += bytes_read;
    }
    fclose(file);

    // Construct the POST request header
    int remainingBodyLength = 0;
    char* contentLengthStr = getContentLength(sockfd, &remainingBodyLength);
    if (remainingBodyLength <= 0) return;
    
    // Read remaining part of the body
    while (remainingBodyLength > 0) {
        ssize_t bytesRead = read(sockfd, buffer, (BUFFER_SIZE - 1 < remainingBodyLength) ? BUFFER_SIZE - 1 : remainingBodyLength);
        if (bytesRead <= 0) {
            perror("Error reading from socket");
            return;
        }
        buffer[bytesRead] = '\0';  // Null-terminate the string
        remainingBodyLength -= bytesRead;

        fwrite(buffer, bytesRead, 1, file);
        printf("%s", buffer);
    }

}

int connect_to_server(const char* restrict domain, const char* restrict port) {
    // Step 1: Get address information for the domain
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // Stream socket (TCP)
    
    int status = getaddrinfo(domain, port, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    // Step 2: Iterate through the available addresses and connect
    int sockfd = -1;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break; // Connected successfully
        }

        close(sockfd); // Close the socket and try the next address
    }

    freeaddrinfo(result); // Free the address information

    if (rp == NULL) {
        fprintf(stderr, "Failed to connect to the domain\n");
        exit(EXIT_FAILURE);
    }

    printf("Connected to the domain successfully!\n");

    return sockfd;
}
