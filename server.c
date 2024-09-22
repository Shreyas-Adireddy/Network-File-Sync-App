#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <openssl/sha.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define MAX_DATA_SIZE 4096

typedef enum {
    LIST,
    DIFF,
    PULL,
    LEAVE
} MessageType;

typedef struct {
    MessageType type;
    size_t data_length;
    char data[MAX_DATA_SIZE];
} Message;

typedef struct {
    int socket;
    struct sockaddr_in address;
} client_t;

void *handle_client(void *arg);
void handle_list(int client_socket);
void handle_diff(int client_socket, const char *client_files);
void handle_pull(int client_socket, const char *filename);
void handle_leave(int client_socket);

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    pthread_t tid[MAX_CLIENTS];

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port 8080...\n");
    int client_count = 0;
    while (1) {
        if (client_count >= MAX_CLIENTS) {
            printf("Maximum clients reached. No more connections allowed.\n");
            while (1);
            continue;
        }
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        client_t *client = malloc(sizeof(client_t));
        memset(client, 0, sizeof(client_t));
        client->socket = new_socket;
        client->address = address;

        if (pthread_create(&tid[client_count], NULL, handle_client, (void*)client) != 0) {
            perror("Failed to create thread");
            free(client);
            close(new_socket);
        }
        pthread_detach(tid[client_count]);
        client_count++;
    }

    return 0;
}

void *handle_client(void *arg) {
    client_t *client = (client_t*)arg;
    int client_socket = client->socket;
    char buffer[BUFFER_SIZE] = {0};
    Message msg;

    printf("New client connected: %s:%d\n", inet_ntoa(client->address.sin_addr), ntohs(client->address.sin_port));

    while (1) {
        int valread = read(client_socket, &msg, sizeof(Message));
        if (valread <= 0) {
            break;
        }

        switch (msg.type) {
            case LIST:
                handle_list(client_socket);
                break;
            case DIFF:
                handle_diff(client_socket, msg.data);
                break;
            case PULL:
                handle_pull(client_socket, msg.data);
                break;
            case LEAVE:
                handle_leave(client_socket);
                close(client_socket);
                free(client);
                printf("Client disconnected\n");
                return NULL;
            default:
                printf("Unknown message type\n");
        }
    }
    return NULL;
}

void handle_list(int client_socket) {
    // Implement directory listing logic here
    printf("Handling LIST request\n");
    // For now, just send a dummy response
    Message response = {LIST, 13, "File1, File2\n"};
    send(client_socket, &response, sizeof(Message), 0);
}

void handle_diff(int client_socket, const char *client_files) {
    // Implement file difference logic here
    printf("Handling DIFF request\n");
    // For now, just send a dummy response
    Message response = {DIFF, 18, "File3\nFile4\nFile5\n"};
    send(client_socket, &response, sizeof(Message), 0);
}

void handle_pull(int client_socket, const char *filename) {
    // Implement file sending logic here
    printf("Handling PULL request for file: %s\n", filename);
    // For now, just send a dummy response
    Message response = {PULL, 14, "File contents"};
    send(client_socket, &response, sizeof(Message), 0);
}

void handle_leave(int client_socket) {
    printf("Handling LEAVE request\n");
    Message response = {LEAVE, 7, "Goodbye"};
    send(client_socket, &response, sizeof(Message), 0);
}