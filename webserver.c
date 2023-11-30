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


// need to look into the difference between writing n blocks of size 1
// and writing 1 block of size n
void write_token_to_file(char* token, char* rest, FILE* file){
    token[(rest-token)-1] = '\r';
    fwrite(token, (rest-token)+1, 1, file);
    token[(rest-token)-1] = '\0';
}

void *connection_handler(void *socket){
    int client_socket = *(int*)socket;
    free(socket); // Free memory allocated for socket descriptor
    char buffer[BUFFERSIZE] = {0}; // Buffer for incoming messages

    char* rest = buffer;
    char* token = 0;

    // Read message from client
    int valread = read(client_socket, buffer, BUFFERSIZE-1); // RESEARCH RECV VS READ
    if (valread < 0) {
        perror("read");
        exit(EXIT_FAILURE);
    }
    buffer[BUFFERSIZE-1] = '\0'; // Null terminate string
    token = strtok_r(rest, "\r\n", &rest); // hold request line

    // Print Incoming Message
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char time_str[100];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);
    // printf("[%s] Incoming Request: %s\n", time_str, token);

    // Print message from client
    char method[10], protocol[10];
    char path[1024] = "public";
    sscanf(token, "%s %s %s", method, path+6, protocol);

    /*
        Note: still needs to handle the case of "/"
    */
    if (strncmp(method, "GET", 3) == 0) {
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
        while(1){
            token = strtok_r(rest, "\r\n", &rest);
            // printf("\n\nrest start\n\n%d\n\nend\n\n", rest[0]);
            if(token == NULL || rest[0] == 0){
                // read next chunk
                // check first if next chunk exits as not to block
                if(token == NULL){
                    printf("token is null\n");
                    break;
                }else{
                    /// could be modified to (BUFFERSIZE - (rest - input)) but this doesn't the cover where the buffer isn't full and might cause problems with garbage
                    /// so you could look into it later
                    /// but this even happen? like we entered here because the buffer wasn't big enough and we are at the end of it
                    /// but still i am not too sure of it so let's just leave it this for now capito?
                    int rest_len = strlen(token);  
                    memcpy(buffer, token, rest_len);
                    valread = recv(client_socket, buffer+rest_len, BUFFERSIZE-1-rest_len, MSG_DONTWAIT);
                    if (valread == -1) {
                        if (errno == EWOULDBLOCK || errno == EAGAIN) {
                            printf("The operation would block.\n");
                            break;
                        } else {
                            perror("Error reading from file");  //// handle error
                            close(client_socket);
                            return NULL;
                        }
                    } else {
                        buffer[rest_len+valread] = '\0';
                        rest = buffer;
                    }
                }
            }else{
                // check if one of the required headers
                // if empty means next we are reading the body
                if((strncmp(token, "Content-Length", 14) == 0) 
                    || (strncmp(token, "Content-Type", 12) == 0) 
                    || (strncmp(token, "Content-Type", 12) == 0)){
                        write_token_to_file(token, rest, file);
                }
            }
        }

        fwrite("\r\n", 2, 1, file);
        

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

        
        // fileBuffer[fileValRead] = '\0'; // Null terminate string

    } else {
        const char *notImplementedMessage = "HTTP/1.1 501 Not Implemented\r\n\r\n";
        send(client_socket, notImplementedMessage, strlen(notImplementedMessage), 0);
    }

    // Source: After finishing the transmission, the server should keep the connection open waiting for new requests from the same client.
    // Source: Close if connection timed out
    close(client_socket); // REMOVE AFTER IMPLEMENTING TIMEOUT ALGORITHM # KEEP TO PREVENT MEMORY LEAKS
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