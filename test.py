import socket

def main():
    host = '127.0.0.1'
    port = 8080

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        message = 'Hello from Python client'
        s.sendall(message.encode())

        data = s.recv(1024);
        print('Received', repr(data))

if __name__ == '__main__':
    main()