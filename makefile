# Default target
build: server client

# Server target
server: server.c
	gcc -Wall -Wextra -g -o server server.c -pthread -lssl -lcrypto

# Client target
client: client.c
	gcc -Wall -Wextra -g -o client client.c -pthread -lssl -lcrypto

# Clean target
clean:
	rm -f server client

# Run server target
run-server: server
	./server

# Run client target
run-client: client
	./client

