#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

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

int sock = 0;
pthread_t receive_thread;

void *receive_messages(void *arg);
void send_message(MessageType type, const char *data);
void handle_user_input();

int main() {
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error \n");
        return -1;
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection Failed \n");
        return -1;
    }

    printf("Connected to server\n");

    if (pthread_create(&receive_thread, NULL, receive_messages, NULL) != 0) {
        perror("Failed to create receive thread");
        return -1;
    }

    handle_user_input();

    pthread_join(receive_thread, NULL);
    close(sock);

    return 0;
}

void *receive_messages(void *arg) {
    Message msg;
    while (1) {
        int valread = read(sock, &msg, sizeof(Message));
        if (valread <= 0) {
            printf("Server disconnected\n");
            break;
        }

        // printf("Received message type: %d\n", msg.type);
        printf("Data: %s\n", msg.data);
    }
    return NULL;
}

void send_message(MessageType type, const char *data) {
    Message msg;
    msg.type = type;
    msg.data_length = strlen(data);
    strncpy(msg.data, data, MAX_DATA_SIZE);
    send(sock, &msg, sizeof(Message), 0);
}

void handle_user_input() {
    char input[BUFFER_SIZE];
    while (1) {
        printf("\nEnter command (LIST, DIFF, PULL <filename>, LEAVE): ");
        fgets(input, BUFFER_SIZE, stdin);
        input[strcspn(input, "\n")] = 0;

        if (strncmp(input, "LIST", 4) == 0) {
            send_message(LIST, "");
        } else if (strncmp(input, "DIFF", 4) == 0) {
            send_message(DIFF, "Client file list");
        } else if (strncmp(input, "PULL", 4) == 0) {
            char *filename = input + 5;
            if (strlen(filename) > 0) {
                send_message(PULL, filename);
            } else {
                printf("Please specify a filename\n");
            }
        } else if (strncmp(input, "LEAVE", 5) == 0) {
            send_message(LEAVE, "");
            break;
        } else {
            printf("Unknown command\n");
        }
    }
}