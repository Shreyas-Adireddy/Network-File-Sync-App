# Default target
build: server client

# Server target
server: server.c
	gcc -Wall -Wextra -g -o ./ser/server server.c -pthread -lssl -lcrypto

# Client target
client: client.c
	gcc -Wall -Wextra -g -o ./clt/client client.c -pthread -lssl -lcrypto

# Clean target
clean:
	rm -f ./clt/* ./ser/server


