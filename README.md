# HTTPReboot
## Rediscovering the Foundations of the Web: A Journey Through HTTP and Sockets

In the early 1990s, Tim Berners-Lee and his team at CERN laid the groundwork for the modern internet by developing the foundational technologies that power the web today. Among these groundbreaking innovations were the Hyper Text Transfer Protocol (HTTP), Hyper Text Markup Language (HTML), and the technology for both a web server and a text-based browser. This trio of technologies revolutionized information sharing, paving the way for the global, interconnected digital world we live in.

Initially, HTTP was a simpler protocol, predominantly characterized by the GET method, which allowed for requesting pages from a server. The simplicity of these early days is often overshadowed by the complex, multifaceted nature of today's web. However, there's immense value in revisiting and understanding these foundational elements. This project is an homage to that pioneering spirit, aiming to deepen our understanding of HTTP by reconstructing its core mechanics from the ground up.

## Part 1: Multi-threaded Web Server
### Introduction & Background
This part of the project focuses on the development of a multi-threaded web server. The server's primary function is to handle HTTP requests using the GET and POST methods, as outlined in the HTTP/1.1 standard.

### Specifications
- <b>Connection Handling</b>: The server is capable of accepting incoming connection requests.
- <b>Request Processing</b>: 
    - For a `GET` request, the server identifies the requested file's name and responds accordingly.
    - For a `POST` request, the server sends an OK message and waits for the uploaded file from the client.
- <b>HTTP Responses</b>:
    - The server responds with `HTTP/1.1 200 OK\r\n` followed by the requested data (in case of GET) and a terminating blank line.
    - If the requested document is not found (in GET requests), the server responds with `HTTP/1.1 404 Not Found\r\n`.
- <b>Connection Persistence</b>: After responding, the server keeps the connection open for new requests from the same client.

### Web-Server Pseudocode
```
while true:
    Listen for connections
    Accept new connection from incoming client and delegate it to a worker thread/process
    Parse HTTP/1.1 request and determine the command (GET or POST)
    If GET, check if target file exists and return error if not found
    If GET, transmit file contents (read from file and write to socket)
    Wait for new requests (persistent connection)
    Close if connection times out
end while
```

## Part 2: HTTP Web Client
### Specifications
In this part of the project, we developed a web client that is capable of sending `GET` and `POST` requests to a web server. The client interacts with the server by reading and parsing commands from an input file.

<b>Command Syntax</b>
- GET Command: `client_get file-path host-name (port-number)`
- POST Command: `client_post file-path host-name (port-number)`

Where `file-path` is the path of the file on the server, including the filename. The port-number is optional; if it is not specified, the client uses the default HTTP port number, which is 80.

<b>Functionality</b>
- Upon receiving a command, the client establishes a connection to the HTTP server on the given host, listening on the specified (or default) port number.
- For `GET` requests, the client downloads the file from the server and stores it in the local directory.
- For `POST` requests, the client uploads the specified file to the server.
- The client continues processing commands until the end of the input file is reached, and then shuts down.

### Web-Client Pseudocode
```
Create a TCP connection with the server
while more operations exist:
    Send the next request to the server
    If GET, receive data from the server
    If POST, send data to the server
end while
Close the connection
```

## Part 3: HTTP 1.1
### Implemented HTTP/1.1 Features
- <b>Persistent Connections</b>: The server keeps connections open after fulfilling a request, allowing clients to make multiple requests over the same connection. *This reduces the overhead of establishing a new connection for each request.*
- <b>Request Pipelining</b>: The server is capable of handling multiple client requests sent in rapid succession, without waiting for each response to be sent before the next request is processed.
- <b>Connection Timeout Heuristic</b>: A mechanism to dynamically manage connection timeouts based on server load.
    - If the server is idle, it allows connections to remain open for a longer period.
    - If the server is busy, it shortens the time an idle connection is kept open to free up resources.