import socket
import threading
import time

# Configuration
SERVER_HOST = 'localhost'
SERVER_PORT = 8080
NUM_CLIENTS = 200  # Number of simulated clients
REQUEST_INTERVAL = 0.1  # Time between requests in seconds
TIMEOUT_CHECK_INTERVAL = 5  # Time between checks on timeouts

# Function for each client behavior
def client_behavior(client_id):
    try:
        # Connect to the server
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((SERVER_HOST, SERVER_PORT))
        print(f"Client {client_id}: Connected")

        # Optionally send a request
        request = f"GET / HTTP/1.1\r\nHost: {SERVER_HOST}\r\n\r\n".encode('utf-8')
        s.sendall(request)
        print(f"Client {client_id}: Request sent")

        # Wait for a response and print it out
        response = s.recv(4096)
        print(f"Client {client_id} received response: {response.decode('utf-8')}")
        
        # Keep the connection open until the server closes it
        while True:
            time.sleep(TIMEOUT_CHECK_INTERVAL)
            try:
                # Try to receive data to check if the connection is still open
                s.setblocking(False)
                data = s.recv(4096)
                if not data:
                    break
            except BlockingIOError:
                # No data received, connection is still open
                pass

        print(f"Client {client_id}: Connection closed by the server")
    except Exception as e:
        print(f"Client {client_id}: Exception occurred: {e}")
    finally:
        s.close()

# Create and start client threads
threads = []
for i in range(NUM_CLIENTS):
    thread = threading.Thread(target=client_behavior, args=(i,))
    threads.append(thread)
    thread.start()
    time.sleep(REQUEST_INTERVAL)

# Wait for all threads to complete
for thread in threads:
    thread.join()

print("All clients have finished.")
