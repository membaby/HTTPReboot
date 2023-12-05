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
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/poll.h>


#define DEFAULT_PORT 8080
#define BACKLOG 100
#define MAX_CONNECTIONS 100000
#define OVERLOAD_RETRY_AFTER 10
#define MAX_TIMEOUT 30.0
#define MAX_REQUESTS 30
#define BUFFER_SIZE 1024
#define DELIMITER "\r\n"
#define DELIMITER_LEN 2
#define TIMEOUT_MSG "Timeout"
#define CONNECTION_CLOSED_MSG "Connection Closed"
#define min(a,b) \
    ({  __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a * (_a<_b) + _b * (_b<=_a); })

#define max(a,b) \
    ({  __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a * (_a>_b) + _b * (_b>=_a); })


int active_connections = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct Connection_attr{
    char buffer[BUFFER_SIZE];
    char* rest;
    char* token;
    bool keep_alive;
    size_t body_length;
    int rest_len;
    struct timeval tv;
    int client_socket;
    int valread;
    int timeout; //in millis
}Connection_attr;

void *connection_handler(void *socket_desc);
void get_handler(Connection_attr* restrict attr, const char* restrict path);
void handle_invalid_request(int client_socket);
void handle_server_overload(int client_socket);
size_t read_next_block(char* restrict buffer, char** rest, const int rest_len, const int client_socket, int timeout);
char* mytok(char* restrict str, const char* restrict delimiter, const size_t delimiter_len, char** rest);
void write_token_to_file(char* restrict token, const char* restrict rest, FILE* restrict file);
int increment_connection_count();
int decrement_connection_count(char message[]);
void logger(const char* restrict message);
int calculate_timeout();


int shutdown_pipe[2] = {-1, -1};
void handle_shutdown(){
    uint64_t u = 1;
    write(shutdown_pipe[1], &u, sizeof(uint64_t));
}


int main(int argc, char const* argv[]){    
    signal(SIGINT, handle_shutdown);
    signal(SIGPIPE, SIG_IGN);
    if (pipe(shutdown_pipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    int server_fd;    // File descriptors for server and client sockets
    struct sockaddr_in address;   // Address structure for IPv4
    int opt = 1;                  // Option value for setsockopt
    int addrlen = sizeof(address);

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
    if (listen(server_fd, BACKLOG) < 0) {     // RESEARCH BACKLOG - WHAT IS THE BEST VALUE?
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // struct timeval tv = {.tv_sec=5, .tv_usec=0};
    // setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&(tv), sizeof(tv));
    
    char log_message[1024];
    snprintf(log_message, sizeof(log_message), "Listening on port %d...", DEFAULT_PORT);
    logger(log_message);

    intptr_t new_socket;
    struct pollfd fds[2];
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;
    fds[1].fd = shutdown_pipe[0];
    fds[1].events = POLLIN;
    int activity;
    while(1) {
        // Accept incoming connection
        activity = poll(fds, 2, -1);
        if (activity < 0) {
            perror("poll");
            break;  // Handle error and exit the loop
        }
        if (fds[0].revents & POLLIN) {
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                continue;
            }
            logger("New Connection Incoming");
            if (active_connections >= MAX_CONNECTIONS) {
                handle_server_overload(new_socket);
                close(new_socket);
                logger("Closed new connection due to server overload");
                continue;
            }

            // Create thread to handle connection
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, connection_handler, (void*)new_socket) < 0) {
                perror("pthread_create");
                continue;
            }

            pthread_detach(thread_id);
        }
        
        if (fds[1].revents & POLLIN) {
            // Read from the eventfd to clear the event
            uint64_t u;
            read(shutdown_pipe[0], &u, sizeof(uint64_t));
            break;
        }
    }
    close(shutdown_pipe[0]);
    close(shutdown_pipe[1]);
    close(server_fd);

    sleep(2);

    exit(EXIT_SUCCESS);
}  

int increment_connection_count() {
    pthread_mutex_lock(&lock);
    active_connections++;
    int snapshot = active_connections;
    pthread_mutex_unlock(&lock);
    char log_message[1024];
    snprintf(log_message, sizeof(log_message), "Active Connections: %d (New Connection)", snapshot);
    logger(log_message);
    return snapshot;
}

int decrement_connection_count(char message[]) {
    pthread_mutex_lock(&lock);
    active_connections--;
    int snapshot = active_connections;
    pthread_mutex_unlock(&lock);
    char log_message[1024];
    snprintf(log_message, sizeof(log_message), "Active Connections: %d (%s)", snapshot, message);
    logger(log_message);
    return snapshot;
}

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
    *rest = end + delimiter_len;
    return token;
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
    fprintf(file, "[%s] %s  --  Connection ID: %ld\n", time_str, message, pthread_self());
    printf("[%s] %s  --  Connection ID: %ld\n", time_str, message, pthread_self());
    fclose(file);
}

int calculate_timeout() {
    int time;
    pthread_mutex_lock(&lock);
    time = MAX_TIMEOUT - (MAX_TIMEOUT / MAX_CONNECTIONS) * active_connections + 1;
    pthread_mutex_unlock(&lock);
    return time;
}


size_t read_next_block(char* restrict buffer, char** rest, const int rest_len, const int client_socket, const int timeout){
    struct pollfd fds[2];
    fds[0].fd = client_socket;
    fds[0].events = POLLIN;
    fds[1].fd = shutdown_pipe[0];
    fds[1].events = POLLIN;

    int activity = poll(fds, 2, timeout);
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
    
    if (fds[1].revents & POLLIN) {
        return -1;
    }

    return -1;
}


void get_handler(Connection_attr* restrict attr, const char* restrict path){
    FILE *file = fopen(path, "rb"); // opens file in binary mode to avoid problems with text files
    if (file == NULL) {
        send(attr->client_socket, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 45, 0);
        return;
    }
    // Read file content
    struct stat st;
    if (stat(path, &st) != 0) {
        send(attr->client_socket, "HTTP/1.1 505 Internal Server Error\r\n\r\n", 38, 0);
        return;
    }
    char header[1024];
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", st.st_size);
    send(attr->client_socket, header, strlen(header), 0);

    size_t total_sent = 0;
    int bytes_read = 0;
    char buffer[st.st_blksize];
    while(total_sent < st.st_size){
        bytes_read = fread(buffer, 1, st.st_blksize, file);
        if(bytes_read == 0)
            break;
        send(attr->client_socket, buffer, bytes_read, 0);
        total_sent += bytes_read;
    }
    fclose(file);
}

size_t handle_body(Connection_attr* attr, const char* restrict path, const bool write){
    // fwrite("\r\n", 2, 1, file);
    // memmove(buffer, rest, rest_len);
    FILE* file;
    if(write)
        file = fopen(path, "wb");
    size_t total_read = min(attr->body_length, attr->rest_len);
    int curr_read = attr->rest_len;
    if(write)
        fwrite(attr->buffer, total_read, 1, file);
    while(total_read < attr->body_length){
        curr_read = read_next_block(attr->buffer, NULL, 0, attr->client_socket, attr->timeout);
        if(curr_read <= 0)
            return -1;
        if(write)
            fwrite(attr->buffer, min(attr->body_length-total_read, curr_read), 1, file);
        if((curr_read == 0) || ((total_read+curr_read) >= attr->body_length)){
            break;
        }
        total_read += curr_read;
    }
    if(write)
        fclose(file);
    
    const size_t body_left = (curr_read<attr->body_length)?(min(attr->body_length-total_read, curr_read)):attr->body_length; 
    attr->rest_len = curr_read-body_left;
    memmove(attr->buffer, (attr->buffer)+body_left, attr->rest_len);
    memset((attr->buffer)+(curr_read-body_left), 0, attr->rest_len);
    // attr->rest = (char*)((attr->buffer)+body_left);
    attr->valread = attr->rest_len;

    
    return total_read+min(attr->body_length-total_read, curr_read);
}


/// should put all of those into a struct
int handle_request_and_headers(Connection_attr* attr, char* restrict method, char* restrict path, char* restrict protocol){
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
            // write_token_to_file(token, rest, file);
        }else if(strncmp(attr->token, "Connection:", 11) == 0){
            if(strncmp(attr->token+12, "keep-alive", 10) == 0){
                attr->keep_alive = true;
            }
        }else if((strncmp(attr->token, "GET", 3) == 0)){
            logger(attr->token);
            sscanf(attr->token, "%s %s %s", method, path+6, protocol);
            if (strcmp(path+6, "/") == 0) {
                memcpy(path+7, "index.html", 10);
            }
        }else if((strncmp(attr->token, "POST", 4) == 0)){
            sscanf(attr->token, "%s %s %s", method, path+6, protocol);
            logger(attr->token);
        }
    }
    return 1;
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



void *connection_handler(void *arg){
    increment_connection_count();
    Connection_attr* attr = (Connection_attr*)calloc(1, sizeof(Connection_attr));
    attr->client_socket = (int)((intptr_t)arg);
    attr->keep_alive = false;
    char method[10], protocol[10];
    char path[1024] = "public";
    short req_count = 0;
    attr->timeout = 5000;
    // attr->tv.tv_sec = 5;
    // attr->tv.tv_usec = 0;
    // setsockopt(attr->client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&(attr->tv), sizeof(attr->tv));

    attr->valread = read_next_block(attr->buffer, NULL, 0, attr->client_socket, attr->timeout);
    if (attr->valread <= 0) {
        perror("valread<=0");
        decrement_connection_count((errno == EWOULDBLOCK)? TIMEOUT_MSG : CONNECTION_CLOSED_MSG);
        close(attr->client_socket);
        free(attr);
        pthread_exit(NULL);
    }
    int ret;
    do {
        attr->rest = attr->buffer, attr->token = 0;
        ret = handle_request_and_headers(attr, method, path, protocol);
        if(ret == -1 || errno == EWOULDBLOCK)
            break;

        if(attr->keep_alive){
            attr->timeout = calculate_timeout()*1000;
        }

        if (strncmp(method, "GET", 3) == 0) {
            get_handler(attr, path);
            ret = handle_body(attr, path, false);
            if(ret == -1 || errno == EWOULDBLOCK)
                break;
        }else if(strncmp(method, "POST", 4) == 0) {
            size_t total_read = handle_body(attr, path, true);
            if(total_read == -1 || errno == EWOULDBLOCK)
                break;
            if(total_read == attr->body_length)
                send(attr->client_socket, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
            else{
                send(attr->client_socket, "HTTP/1.1 408 OK\r\n\r\n", 19, 0);
            }
        }else if(strncmp(method, "Forbidden", 9) == 0){
            send(attr->client_socket, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
            ret = handle_body(attr, path, false);
            if(ret == -1 || errno == EWOULDBLOCK)
                break;
        }else {
            handle_invalid_request(attr->client_socket);
            ret = handle_body(attr, path, false);
            if(ret == -1 || errno == EWOULDBLOCK)
                break;
        }
        req_count++;
    }while(((attr->valread > 0) || (attr->keep_alive)) && (errno != EWOULDBLOCK) && (req_count<MAX_REQUESTS));
    decrement_connection_count((errno == EWOULDBLOCK)? TIMEOUT_MSG : CONNECTION_CLOSED_MSG);
    close(attr->client_socket);
    free(attr);
    pthread_exit(NULL);
}



