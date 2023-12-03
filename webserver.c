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

#define DEFAULT_PORT 8080
#define BUFFERSIZE 1024
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

size_t read_next_block(char* restrict buffer, char** rest, const int rest_len, const int client_socket){
    /// could be modified to (BUFFERSIZE - (rest - input)) but this doesn't the cover where the buffer isn't full and might cause problems with garbage
    /// so you could look into it later
    /// but this even happen? like we entered here because the buffer wasn't big enough and we are at the end of it
    /// but still i am not too sure of it so let's just leave it this for now capito?
    // (block)? 0 : MSG_DONTWAIT
    //  

    int valread = recv(client_socket, buffer+rest_len, BUFFERSIZE-1-rest_len, 0);
    if (valread == -1) {
        perror("timedout reading from socket");
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

size_t handle_body(char* restrict buffer, const char* restrict path, char** restrict rest, 
                                int* rest_len, const size_t body_length, 
                                const int client_socket, bool write, int* valread){
    // fwrite("\r\n", 2, 1, file);
    // memmove(buffer, rest, rest_len);
    FILE* file;
    if(write)
        file = fopen(path, "wb");
    size_t total_read = min(body_length, *rest_len), curr_read = *rest_len;
    if(write)
        fwrite(buffer, total_read, 1, file);
    while(total_read < body_length){
        curr_read = read_next_block(buffer, NULL, 0, client_socket);
        if(write)
            fwrite(buffer, min(body_length-total_read, curr_read), 1, file);
        if((curr_read == 0) || ((total_read+curr_read) >= body_length)){
            break;
        }else if(curr_read == -1){
            close(client_socket);
            // free(buffer);
            return -1; // handle later
        }
        total_read += curr_read;
    }
    if(write)
        fclose(file);
    
    const size_t body_left = (curr_read<body_length)?(min(body_length-total_read, curr_read)):body_length; 
    *rest_len = curr_read-body_left;
    // if(curr_read == BUFFERSIZE-1){
    //     curr_read = read_next_block(buffer, rest, *rest_len, client_socket);
    // printf("before memeset\ncurr:%ld\nbodyleft:%ld\ntotalread:%ld\nbodtlen:%ld\n", curr_read, body_left, total_read, body_length);
    // }else{
        memmove(buffer, buffer+body_left, *rest_len);
        memset(buffer+(curr_read-body_left), 0, *rest_len);
        *rest = buffer+body_left;
        *valread = *rest_len;
    // }
    return total_read;
}

void get_handler(char* restrict buffer, const char* restrict path, const int client_socket, int* valread){
    // while(*valread == BUFFERSIZE-1){
    //     *valread = read_next_block(buffer, NULL, 0, client_socket);
    // }
    // remeber to set rest len after reading the body of the get
    FILE *file = fopen(path, "rb"); // opens file in binary mode to avoid problems with text files
    if (file == NULL) {
        send(client_socket, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
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




/// should put all of those into a struct
void handle_request_and_headers(char* buffer, char* token, char* rest,
                int* rest_len, int* valread, int client_socket, 
                size_t* body_length, char* restrict method, char* restrict path, 
                char* restrict protocol, bool* keep_alive){
    while(1){
        token = mytok(rest, DELIMITER, DELIMITER_LEN, &rest);
        // read next chunk
        // check first if next chunk exits as not to block
        if(token != NULL && token[0] == '\0'){
            //reached \r\n\r\n
            *rest_len = (*valread)-(rest-buffer);
            memmove(buffer, rest, *rest_len);
            break;
        }
        if(token == NULL){
            *rest_len = (*valread)-(rest-buffer);
            memmove(buffer, rest, *rest_len);
            *valread = read_next_block(buffer, &rest, *rest_len, client_socket);
            if(*valread == 0){
                memmove(buffer, rest, *rest_len);
                break;
            }else if((*valread) == -1){
                close(client_socket);
                // free(buffer);
                return; //handle later
            }
            continue;
        }
        // printf("\n----------------\ntoken %s\n----------------\n", token);
        if(strncmp(token, "Content-Length:", 15) == 0){
            *body_length = atoll(token+16);
            // write_token_to_file(token, rest, file);
        }else if(strncmp(token, "Content-Type:", 13) == 0){
            // write_token_to_file(token, rest, file);
        }else if(strncmp(token, "Content-Encoding:", 17) == 0){
            // write_token_to_file(token, rest, file);
        }else if(strncmp(token, "Connection:", 11) == 0){
            if(strncmp(token+12, "keep-alive", 10))
                *keep_alive = true;
        }else if((strncmp(token, "GET", 3) == 0)){
            time_t now = time(NULL);
            struct tm *tm_now = localtime(&now);
            char time_str[100];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);
            printf("[%s] Incoming Request: %s\n", time_str, token);
            sscanf(token, "%s %s %s", method, path+6, protocol);
            if (strcmp(path+6, "/") == 0) {
                memmove(path+7, "index.html", 10);
            }
        }else if((strncmp(token, "POST", 4) == 0)){
            sscanf(token, "%s %s %s", method, path+6, protocol);
            if (strcmp(path+6, "/") == 0) {
                send(client_socket, "HTTP/1.1 403 Forbidden\r\n\r\n", 26, 0);
            }
        }
    }
}

void *connection_handler(void *socket){
    int client_socket = (int)((intptr_t)socket);
    printf("Hehe\n");
    char* buffer = calloc(BUFFERSIZE, sizeof(char)); // Buffer for incoming messages
    int timeout = 1;
    char* rest;
    char* token = 0;
    bool keep_alive = false;
    char method[10], protocol[10];
    char path[1024] = "public";

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Read message from client
    // int valread = read(client_socket, buffer, BUFFERSIZE-1); // RESEARCH RECV VS READ
    int valread = read_next_block(buffer, NULL, 0, client_socket);
    if (valread <= 0) {
        free(buffer);
        close(client_socket);
        pthread_exit(NULL);
    }

    // Print message from client
    /*
        Note: this still doesn't handle the case where a key and value pair in the header is larger than the buffer size
        but neglict for now and look into later
    */
    //parse request and headers
    size_t body_length = 0;
    int rest_len = 0;
    short null_count = 0;
    do{
        rest = buffer, token = 0;
        handle_request_and_headers(buffer, token, rest,
                    &rest_len, &valread, 
                    client_socket, &body_length, 
                    method, path, protocol, &keep_alive);

        if(keep_alive){
            tv.tv_sec = (keep_alive)? 6 : tv.tv_sec;
            tv.tv_usec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        }

        if (strncmp(method, "GET", 3) == 0) {
            get_handler(buffer, path, client_socket, &valread);
            handle_body(buffer, path, &rest, &rest_len, body_length, client_socket, false, &valread);
        } else if (strncmp(method, "POST", 4) == 0) {
            size_t total_read = handle_body(buffer, path, &rest, &rest_len, body_length, client_socket, true, &valread);
            if(total_read == body_length)
                send(client_socket, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);
            else
                send(client_socket, "HTTP/1.1 408 OK\r\n\r\n", 19, 0);
        } else {
            send(client_socket, "HTTP/1.1 501 Not Implemented\r\n\r\n", 32, 0);
        }
        printf("\n=========\nBuffer:%.*s\n---------\n", (valread), buffer);
    }while(valread > 0);

    // Source: After finishing the transmission, the server should keep the connection open waiting for new requests from the same client.
    // Source: Close if connection timed out
    printf("\n=========\nDONE\n---------\n");
    free(buffer);
    close(client_socket); // REMOVE AFTER IMPLEMENTING TIMEOUT ALGORITHM # KEEP TO PREVENT MEMORY LEAKS
    pthread_exit(NULL);
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
    // if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) { // RESEARCH SO_REUSEADDR
    //     perror("setsockopt");
    //     exit(EXIT_FAILURE);
    // }
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
    
    printf("Listening on port %d...\n", DEFAULT_PORT);
    
    intptr_t new_socket;
    while(1) {
        // Accept incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
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



    close(server_fd);

    exit(EXIT_SUCCESS);
}  