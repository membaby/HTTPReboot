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
    /// could be modified to (BUFFERSIZE - (rest - input)) but this doesn't the cover where the buffer isn't full and might cause problems with garbage
    /// so you could look into it later
    /// but this even happen? like we entered here because the buffer wasn't big enough and we are at the end of it
    /// but still i am not too sure of it so let's just leave it this for now capito?

    int valread = recv(client_socket, buffer+rest_len, BUFFERSIZE-1-rest_len, MSG_DONTWAIT);
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

void *connection_handler(void *socket){
    int client_socket = *(int*)socket;
    free(socket); // Free memory allocated for socket descriptor
    char* buffer = calloc(BUFFERSIZE, sizeof(char)); // Buffer for incoming messages

    char* rest = buffer;
    char* token = 0;


    // Read message from client
    int valread = read(client_socket, buffer, BUFFERSIZE-1); // RESEARCH RECV VS READ
    // int valread = read_next_block(buffer, NULL, 0, client_socket);
    if (valread <= 0) {
        free(buffer);
        close(client_socket);
        return NULL;
    }
    buffer[BUFFERSIZE-1] = '\0'; // Null terminate string
    token = mytok(rest, DELIMITER, DELIMITER_LEN, &rest); // hold request line

    // Print Incoming Message
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char time_str[100];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);
    printf("[%s] Incoming Request: %s\n", time_str, token);

    // Print message from client
    char method[10], protocol[10];
    char path[1024] = "public";
    sscanf(token, "%s %s %s", method, path+6, protocol);
    if (strcmp(path, "public/") == 0) {
        strcat(path, "index.html");
    }

    /*
        Note: still needs to handle the case of "/"
    */
    if (strncmp(method, "GET", 3) == 0) {
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
    } else if (strncmp(method, "POST", 4) == 0) {
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
                    return NULL;
                }
                continue;
            }
            // printf("\n----------------\ntoken %s\n----------------\n", token);
            if(strncmp(token, "Content-Length:", 15) == 0){
                body_length = atoll(token+16);
                // write_token_to_file(token, rest, file);
            }else if(strncmp(token, "Content-Type:", 13) == 0){
                // write_token_to_file(token, rest, file);
            }else if(strncmp(token, "Content-Encoding:", 17) == 0){
                // write_token_to_file(token, rest, file);
            }
        }

        // fwrite("\r\n", 2, 1, file);
        memmove(buffer, rest, rest_len);

        size_t total_read = 0, curr_read = rest_len;
        while(total_read <= body_length){
            fwrite(buffer, curr_read, 1, file);
            total_read += curr_read;
            // printf("\n----------------\nbuffer: %s\n----------------\n", buffer);
            curr_read = read_next_block(buffer, NULL, 0, client_socket);
            if(curr_read == 0){
                break;
            }else if(curr_read == -1){
                close(client_socket);
                free(buffer);
                return NULL;
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


    } else {
        const char *notImplementedMessage = "HTTP/1.1 501 Not Implemented\r\n\r\n";
        send(client_socket, notImplementedMessage, strlen(notImplementedMessage), 0);
    }

    // Source: After finishing the transmission, the server should keep the connection open waiting for new requests from the same client.
    // Source: Close if connection timed out
    close(client_socket); // REMOVE AFTER IMPLEMENTING TIMEOUT ALGORITHM # KEEP TO PREVENT MEMORY LEAKS
    free(buffer);
    return NULL;
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
    
    printf("Listening on port %d...\n", DEFAULT_PORT);

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