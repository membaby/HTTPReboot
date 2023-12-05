#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <stdbool.h>
#include <ctype.h>


#define BUFFER_SIZE 5000
#define DELIMITER "\r\n"
#define DELIMITER_LEN 2
#define min(a,b) \
    ({  __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a * (_a<_b) + _b * (_b<=_a); })

#define max(a,b) \
    ({  __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a * (_a>_b) + _b * (_b>=_a); })


typedef struct Connection_attr{
    char buffer[BUFFER_SIZE];
    char* rest;
    char* token;
    bool chunked;
    size_t body_length;
    int rest_len;
    struct timeval tv;
    int client_socket;
    int valread;
    int timeout; //in millis
}Connection_attr;


void process_commands();
void handle_get_request(int sockfd, const char *path, const char *output, const char* restrict host);
void handle_post_request(int sockfd, const char *path, const char* output);
int connect_to_server(const char* restrict domain, const char* restrict port);



int main(int argc, char *argv[]) {
    // Check the number of arguments
    // if (argc != 3) {
    //     fprintf(stderr, "Usage: %s server_ip port_number\n", argv[0]);
    //     return EXIT_FAILURE;
    // }


    // Process commands from the user
    process_commands();

    // Close the socket
    
    return EXIT_SUCCESS;
}

void process_commands() {
    FILE *file = fopen("input.txt", "r");
    char line[BUFFER_SIZE];

    if (!file) {
        perror("Error opening file");
        return;
    }

    // Read and process each line from the file
    while (fgets(line, BUFFER_SIZE, file)) {
        char method[15], path[BUFFER_SIZE], domain[1024], port[15], output[100];
        if (sscanf(line, "%s %s %s %s -o %s", method, path, domain, port, output) != 5) {
            fprintf(stderr, "Invalid command format\n");
            continue;
        }

        printf("\n\nSending Request: Method: %s, Path: %s\n", method, path);

        // Connect to the server (Open a TCP connection)
        int sockfd = connect_to_server(domain, port);
        if (sockfd < 0) {
            fprintf(stderr, "Failed to connect to the server\n");
            return;
        }

        if (strncmp(method, "client_get", 10) == 0) {
            handle_get_request(sockfd, path, output, domain);
        } else if (strncmp(method, "client_post", 11) == 0) {
            handle_post_request(sockfd, path, output);
        } else {
            fprintf(stderr, "Unsupported method\n");
        }

        close(sockfd);
    }

    fclose(file);
}


void write_token_to_file(char* restrict token, const char* restrict rest, FILE* restrict file){
    token[(rest-token)-2] = '\r';
    token[(rest-token)-1] = '\n';
    fwrite(token, (rest-token), 1, file);
    token[(rest-token)-2] = '\0';
    token[(rest-token)-1] = '\0';
}

char* mytok(char* restrict str, const char* restrict delimiter, const size_t delimiter_len, char** rest){
    char* token = str;
    char* end = strstr(str, "\r\n");
    if(end == NULL){
        return NULL;
    }
    for(int i=0;i<delimiter_len;i++)
        end[i] = '\0';
    *rest = end + delimiter_len;
    return token;
}

size_t read_next_block(char* restrict buffer, char** rest, const int rest_len, const int client_socket, const int timeout){
    struct pollfd fds[2];
    fds[0].fd = client_socket;
    fds[0].events = POLLIN;

    int activity = poll(fds, 1, timeout);
    if (activity < 0) {
        perror("poll");
        return -1;  // Handle error and exit the loop
    }
    if (fds[0].revents & (POLLIN | POLLERR)) {
        int curr_read = recv(client_socket, buffer+rest_len, BUFFER_SIZE-1-rest_len, 0);
        if (curr_read <= -1) {
            perror("timedout reading from socket");
            return -1;
        } else {
            buffer[rest_len+curr_read] = '\0';
            if(rest != NULL)
                *rest = buffer;
        }
        return curr_read+rest_len;
    }
    

    return -1;
}

bool isHexa(char* str){
    bool isHex = false;
    char* ptr = str;
    if (ptr == NULL || *ptr == '\0') 
        return false;
    // Check each character in the string
    while (*ptr != '\0') {
        if (!isxdigit(*ptr)) {
            return false; // Not a hexadecimal digit
        }
        ptr++;
    }
    return true;
}

int handle_chunk(Connection_attr* attr, FILE* file, int curr_read){
    attr->rest = attr->buffer, attr->token = 0, attr->rest_len=0, attr->token = attr->buffer;
    char* start;
    long len = 0;
    while(attr->token != NULL){
        start = attr->token;
        attr->token = mytok(attr->rest, DELIMITER, DELIMITER_LEN, &attr->rest);
        if(attr->token == NULL){
            fwrite(start, curr_read - (int)(attr->rest - start), 1, file);
            break;
        }else if(isHexa(attr->token)){
            len = strtol(attr->token, NULL, 16);
            if(len == 0)
                return -1;
        }else{
            fwrite(attr->token, attr->rest-attr->token, 1, file);
        }   
    }
    return 1;
}

void handle_body(Connection_attr* attr, FILE* file){
    // fwrite("\r\n", 2, 1, file);
    // memmove(buffer, rest, rest_len);

    size_t total_read = min(attr->body_length, attr->rest_len);
    int curr_read = attr->rest_len;
    if(attr->chunked){
        if(handle_chunk(attr, file, curr_read) < 0)
            return;
    }else
        fwrite(attr->buffer, total_read, 1, file);

    while((attr->chunked) || (total_read < attr->body_length)){
        curr_read = read_next_block(attr->buffer, NULL, 0, attr->client_socket, attr->timeout);
        if(curr_read <= 0)
            return;
        if(attr->chunked){ //assuming currread > 4
            if(handle_chunk(attr, file, curr_read) < 0)
                return;
        }else{
            fwrite(attr->buffer, min(attr->body_length-total_read, curr_read), 1, file);
            if((curr_read == 0) || ((total_read+curr_read) >= attr->body_length)){
                break;
            }
            total_read += curr_read;
        }
    }
    
}




/// should put all of those into a struct
int handle_request_and_headers(Connection_attr* attr, FILE* file){
    while(1){
        attr->token = mytok(attr->rest, DELIMITER, DELIMITER_LEN, &attr->rest);
        // read next chunk
        // check first if next chunk exits as not to block
        if(attr->token != NULL && attr->token[0] == '\0'){
            //reached \r\n\r\n
            attr->rest_len = attr->valread-(attr->rest-attr->buffer);
            memmove(attr->buffer, attr->rest, attr->rest_len);
            break;
        }
        
        if(attr->token == NULL){
            attr->rest_len = (attr->valread)-(attr->rest-attr->buffer);
            memmove(attr->buffer, attr->rest, attr->rest_len);
            attr->valread = read_next_block(attr->buffer, &attr->rest, attr->rest_len, attr->client_socket, attr->timeout);
            if((attr->valread) <= 0)
                return -1;

            continue;
        }
        
        if(strncmp(attr->token, "Content-Length:", 15) == 0){  
            attr->body_length = atoll(attr->token+16);
        }else if(strncmp(attr->token, "Transfer-Encoding: chunked", 26) == 0){  
            attr->chunked = true;
        }
        printf("%s\r\n", attr->token);
        // write_token_to_file(attr->token, attr->rest, file);
        
    }
    return 1;
}


void handle_response(const int sockfd, const char *path){ 
    Connection_attr* attr = (Connection_attr*)calloc(1, sizeof(Connection_attr));
    attr->client_socket = sockfd;
    attr->chunked = false;
    char method[10], protocol[10];
    short req_count = 0;
    attr->timeout = 10000;
    char filePath[1024] = "files/";
    if (strcmp(path, "/") == 0) {
        memcpy(filePath+5, "/index.html", 11);
    }else{
        strcat(filePath, path);
    }
    printf("path: %s\n", filePath);
    FILE* file = fopen(filePath, "wb"); 
    if (file == NULL) {
        perror("Error opening file");
        free(attr);
        return;
    }
    attr->valread = read_next_block(attr->buffer, NULL, 0, attr->client_socket, attr->timeout);
    if (attr->valread <= 0) {
        perror("valread<=0");
        close(attr->client_socket);
        free(attr);
        return;
    }
    attr->rest = attr->buffer, attr->token = 0;
    handle_request_and_headers(attr, file);
    handle_body(attr, file);
    free(attr);
    fclose(file);
}



void handle_get_request(const int sockfd, const char *path, const char* output, const char* restrict host) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);
    // Send the GET request
    int n = send(sockfd, buffer, strlen(buffer), 0);
    if (n < 0) {
        perror("Error writing to socket");
        return;
    }

    handle_response(sockfd, output);
}

void handle_post_request(int sockfd, const char *path, const char* output) {
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
    if (stat(filePath, &st) != 0) {
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

    handle_response(sockfd, output);
}

int connect_to_server(const char* restrict domain, const char* restrict port) {
    // Step 1: Get address information for the domain
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;    // Allow IPv4 or IPv6
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
