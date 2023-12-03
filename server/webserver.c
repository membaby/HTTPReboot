#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

#define DEFAULT_PORT 8080
#define MAX_CONNECTIONS 100000
#define OVERLOAD_RETRY_AFTER 10
#define MAX_TIMEOUT 30.0
#define BUFFER_SIZE 1024
#define DELIMITER "\r\n"
#define DELIMITER_LEN 2

int active_connections = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *connection_handler(void *socket_desc);
void handle_get_request(int client_socket, const char *path, char* buffer);
void handle_post_request(int client_socket, const char *path, char* buffer, char* rest, char* token, int valread);
void handle_invalid_request(int client_socket);
void handle_server_overload(int client_socket);
int read_next_block(char* restrict buffer, char** rest, const size_t rest_len, const int client_socket);
char* mytok(char* restrict str, const char* restrict delimiter, const size_t delimiter_len, char** rest);
void write_token_to_file(char* restrict token, const char* restrict rest, FILE* restrict file);
int increment_connection_count();
int decrement_connection_count(char message[64]);
void logger(const char* restrict message);
struct timeval calculate_timeout();

// need to look into the difference between writing n blocks of size 1
// and writing 1 block of size n
void write_token_to_file(char* restrict token, const char* restrict rest, FILE* restrict file){
    token[(rest-token)-2] = '\r';
    token[(rest-token)-1] = '\n';
    fwrite(token, (rest-token), 1, file);
    token[(rest-token)-2] = '\0';
    token[(rest-token)-1] = '\0';
}

char* mytok(char* restrict str, const char* restrict delimiter, const size_t delimiter_len, char** rest){
    char* token = str;
    char* end = strstr(str, delimiter);
    if(end == NULL){
        return NULL;
    }
    for(int i=0;i<delimiter_len;i++)
        end[i] = '\0';
    *rest = end + 2;
    return token;
}

int read_next_block(char* restrict buffer, char** rest, const size_t rest_len, const int client_socket){
    /// could be modified to (BUFFER_SIZE - (rest - input)) but this doesn't the cover where the buffer isn't full and might cause problems with garbage
    /// so you could look into it later
    /// but this even happen? like we entered here because the buffer wasn't big enough and we are at the end of it
    /// but still i am not too sure of it so let's just leave it this for now capito?

    int valread = recv(client_socket, buffer+rest_len, BUFFER_SIZE-1-rest_len, MSG_DONTWAIT);
    if (valread == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0;
        } else {
            perror("Error reading from socket");  //// handle error
            return -1;
        }
    } else {
        buffer[rest_len+valread] = '\0';
        if(rest != NULL)
            *rest = buffer;
    }
    return valread+rest_len;
}

void handle_get_request(int client_socket, const char *path, char* buffer) {
    while(read_next_block(buffer, NULL, 0, client_socket) > 0); // read remaining message from client
    FILE *file = fopen(path, "rb"); // opens file in binary mode to avoid problems with text files
    if (file == NULL) {
        const char *notFoundMessage = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(client_socket, notFoundMessage, strlen(notFoundMessage), 0);
    } else {
        // Read file content
        fseek(file, 0, SEEK_END);
        long fsize = ftell(file);
        fseek(file, 0, SEEK_SET);
        char *data = calloc(fsize + 1, sizeof(char));
        fread(data, 1, fsize, file);
        fclose(file);
        data[fsize] = 0;

        // Send file content to client
        char header[1024];
        sprintf(header, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", fsize);
        send(client_socket, header, strlen(header), 0);
        send(client_socket, data, fsize, 0);       // RESEARCH SENDING 1 VS SENDING 2 MESSAGES
        free(data);
    }
}

void handle_post_request(int client_socket, const char *path, char* buffer, char* rest, char* token, int valread) {
    /*
        Note: this still doesn't handle the case where a key and value pair in the header is larger than the buffer size
        but neglict for now and look into later
    */

    FILE* file = fopen(path, "wb");
    size_t body_length = -1;
    size_t rest_len = 0;
    short null_count = 0;
    while(1){
        token = mytok(rest, DELIMITER, DELIMITER_LEN, &rest);
        // read next chunk
        // check first if next chunk exits as not to block
        if(token != NULL && token[0] == '\0'){
            //reached \r\n\r\n
            rest_len = valread-(rest-buffer);
            break;
        }
        if(token == NULL){
            rest_len = valread-(rest-buffer);
            memmove(buffer, rest, rest_len);
            valread = read_next_block(buffer, &rest, rest_len, client_socket);
            if(valread == 0){
                memmove(buffer, rest, rest_len);
                break;
            }else if(valread == -1){
                close(client_socket);
                free(buffer);
                return;
            }
            continue;
        }
    }

    memmove(buffer, rest, rest_len);

    size_t total_read = 0, curr_read = rest_len;
    while(total_read <= body_length){
        fwrite(buffer, curr_read, 1, file);
        total_read += curr_read;
        curr_read = read_next_block(buffer, NULL, 0, client_socket);
        if(curr_read == 0){
            break;
        }else if(curr_read == -1){
            close(client_socket);
            free(buffer);
            return;
        }
    }

    fclose(file);

    /*
        DON't FORGET TO WRITE \r\n AFTER THE 3 HEADER LINES TO NOTE THE START OF THE FILE
        but then in the get we need to check wether those header lines exist or not
        if exist just add them to the header part
        if not then just put the file in the body and care no more
        here you could finally start writing the body of the request into the file
    */

    const char *okMessage = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_socket, okMessage, strlen(okMessage), 0);
}

void handle_invalid_request(int client_socket) {
    const char *badRequestMessage = "HTTP/1.1 400 Bad Request\r\n\r\n";
    send(client_socket, badRequestMessage, strlen(badRequestMessage), 0);
}

void handle_server_overload(int client_socket) {
    char responseMessage[256];
    
    // Build the HTTP response string.
    snprintf(responseMessage, sizeof(responseMessage),
             "HTTP/1.1 503 Service Unavailable\r\n"
             "Retry-After: %d\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<html><body><h1>503 Service Unavailable</h1></body></html>\r\n",
             OVERLOAD_RETRY_AFTER);

    // Send the custom error message over the client's socket.
    send(client_socket, responseMessage, strlen(responseMessage), 0);
}

void logger(const char* restrict message){
    FILE* file = fopen("log.txt", "a");
    if(file == NULL){
        perror("Error opening log file");
        return;
    }
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char time_str[100];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);
    fprintf(file, "[%s] %s\n", time_str, message);
    printf("[%s] %s\n", time_str, message);
    fclose(file);
}

struct timeval calculate_timeout() {
    struct timeval tv;
    pthread_mutex_lock(&lock);
    tv.tv_sec = MAX_TIMEOUT - (MAX_TIMEOUT / MAX_CONNECTIONS) * active_connections + 1;
    pthread_mutex_unlock(&lock);
    tv.tv_usec = 0;
    return tv;
}

void *connection_handler(void *socket_desc){
    int client_socket = *(int*)socket_desc;
    free(socket_desc);
    int snapshot = increment_connection_count();
    if (snapshot >= MAX_CONNECTIONS) {
        handle_server_overload(client_socket);
        decrement_connection_count("ERR503 - Server Overload");
        close(client_socket);
        return NULL;
    }
    int activity;
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        struct timeval tv = calculate_timeout(); // dynamic timeout based on load
        activity = select(client_socket + 1, &read_fds, NULL, NULL, &tv);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            continue;
        }
        if (activity == 0) {
            break;
        }
        if (FD_ISSET(client_socket, &read_fds)) {
            char* buffer = calloc(BUFFER_SIZE, sizeof(char)); // Buffer for incoming messages
            if (!buffer) {
                perror("calloc");
                continue;
            }
            char* rest = buffer;
            char* token = 0;

            // Read message from client
            int valread = read(client_socket, buffer, BUFFER_SIZE-1); // RESEARCH RECV VS READ
            if (valread <= 0) {
                // If read error or client closed connection, break the loop
                free(buffer);
                continue;
            }
            buffer[BUFFER_SIZE-1] = '\0';
            token = mytok(rest, DELIMITER, DELIMITER_LEN, &rest); // hold request line

            char log_message[1024];
            sprintf(log_message, "Incoming Request: %s", token);
            logger(log_message);

            // Print message from client
            char method[10], protocol[10];
            char path[1024] = "public";
            sscanf(token, "%s %s %s", method, path+6, protocol);

            // Special Case: If path is "/", return index.html
            if (strcmp(path, "public/") == 0) {
                strcat(path, "index.html");
            }

            if (strncmp(method, "GET", 3) == 0) {
                handle_get_request(client_socket, path, buffer);
            } else if (strncmp(method, "POST", 4) == 0) {
                handle_post_request(client_socket, path, buffer, rest, token, valread);
            } else {
                handle_invalid_request(client_socket);
            }
            free(buffer);
        }
    }
    decrement_connection_count(activity == 0 ? "Timeout" : "Connection Closed");
    close(client_socket);
    return NULL;
}

int increment_connection_count() {
    pthread_mutex_lock(&lock);
    active_connections++;
    int snapshot = active_connections;
    pthread_mutex_unlock(&lock);
    char log_message[1024];
    sprintf(log_message, "Active Connections: %d (New Connection)", snapshot);
    logger(log_message);
    return snapshot;
}

int decrement_connection_count(char message[64]) {
    pthread_mutex_lock(&lock);
    active_connections--;
    int snapshot = active_connections;
    pthread_mutex_unlock(&lock);
    char log_message[1024];
    sprintf(log_message, "Active Connections: %d (%s)", snapshot, message);
    logger(log_message);
    return snapshot;
}

int main(int argc, char const* argv[]){    
    int server_fd;    // File descriptors for server and client sockets
    struct sockaddr_in address;   // Address structure for IPv4
    int opt = 1;                  // Option value for setsockopt
    int addrlen = sizeof(address);
    char buffer[1024] = {0};      // Buffer for incoming messages

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Attach socket to port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { // RESEARCH SO_REUSEADDR
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
    if (listen(server_fd, 3) < 0) {     // RESEARCH BACKLOG - WHAT IS THE BEST VALUE?
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    char log_message[1024];
    sprintf(log_message, "Listening on port %d...", DEFAULT_PORT);
    logger(log_message);

    while(1) {
        // Accept incoming connection
        int *new_socket = calloc(1, sizeof(int));
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