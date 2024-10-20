# Default target
build: server client

# Server target
server: server.c
	gcc -Wall -Wextra -g -o ./server/server server.c -pthread -lssl -lcrypto

# Client target
client: client.c
	gcc -Wall -Wextra -g -o ./client/client client.c -pthread -lssl -lcrypto

# Clean target
clean:
	rm -f ./client/* ./server/server


