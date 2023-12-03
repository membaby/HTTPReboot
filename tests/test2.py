import socket
import concurrent.futures
import time

# Configuration
SERVER_HOST = 'localhost'
SERVER_PORT = 8080
NUM_CLIENTS = 2000  # Number of simulated clients

TIME_TAKEN = []
STATUSES = {
    '200': 0,
    '503': 0
}
# Function for each client behavior
def client_behavior(client_id):
    x = ''
    try:
        # Connect to the server
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((SERVER_HOST, SERVER_PORT))
        # print(f"Client {client_id}: Connected")
        x += '1'

        # Optionally send a request
        request = f"GET / HTTP/1.1\r\nHost: {SERVER_HOST}\r\n\r\n".encode('utf-8')
        s.sendall(request)
        x += '2'
        # print(f"Client {client_id}: Request sent")

        # Wait for a response and print it out
        start_time = time.time()
        response = s.recv(4096)
        x += '3'
        # print(f"Client {client_id} received response")
        if '503' in response.decode('utf-8'):
            STATUSES['503'] += 1
        elif '200' in response.decode('utf-8'):
            STATUSES['200'] += 1

        end_time = time.time()
        TIME_TAKEN.append(end_time - start_time)
        x += '4'

        s.close()
    except Exception as e:
        print(f"Client {client_id} ({x}): Exception occurred: {e}")
        

if __name__ == '__main__':
    executor = concurrent.futures.ThreadPoolExecutor(max_workers=NUM_CLIENTS)
    for i in range(NUM_CLIENTS):
        executor.submit(client_behavior, i)
    executor.shutdown(wait=True)

    print("All clients have finished.")
    print('Average time taken: ', sum(TIME_TAKEN)/len(TIME_TAKEN), 'seconds')
    print('200: ', STATUSES['200'])
    print('503: ', STATUSES['503'])

    print(len(TIME_TAKEN))