import socket
import concurrent.futures

def make_request(host, port, method, path='/', body=None):

    if method == 'GET':
        request = f'GET {path} HTTP/1.1\r\nHost: {host}\r\n\r\n'
    elif method == 'POST':
        if body is None:
            raise ValueError('Body must be provided for POST requests')
        request = f'POST {path} HTTP/1.1\r\nHost: {host}\r\nContent-Length: {len(body)}\r\n\r\n{body}'

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.sendall(request.encode())
        data = s.recv(4096);
        print('Received:', data.decode())

if __name__ == '__main__':
    host = '127.0.0.1'
    port = 8080
    
    # Sending a GET request
    # make_request(host, port, 'GET', 'file.txt', None)

    # Sending a POST request
    make_request(host, port, 'POST', 'file.txt', 'Hello from Python client')

    # executor = concurrent.futures.ThreadPoolExecutor(max_workers=32)
    # futures = [executor.submit(make_request) for i in range(100)]
    # concurrent.futures.wait(futures)
