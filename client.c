#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024
#define MAX_DATA_SIZE 4096
#define MAX_FILES 512

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

void send_message(MessageType type);
void send_message_pull(Message* diff);
void handle_user_input();
void receive_and_save_files();
void receive_and_save_diff(Message* diff);
int crawl_directory(char* data);

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

    handle_user_input();
    close(sock);

    return 0;
}

void send_message(MessageType type) {
    Message msg;
    msg.type = type;
    msg.data_length = 0;
    if (type == DIFF){
        int num_files = 0;
        if ((num_files = crawl_directory(msg.data)) < 0){
            perror("DIFF failed to crawl dir");
            return;
        }
        msg.data_length = num_files;
    }
    send(sock, &msg, sizeof(Message), 0);
}

void handle_user_input() {
    char input[BUFFER_SIZE] = {0};
    Message diff;
    memset(&diff, 0, sizeof(diff));
    while (1) {
        printf("\nEnter command (LIST, DIFF, PULL, LEAVE): ");
        fgets(input, BUFFER_SIZE, stdin);
        input[strcspn(input, "\n")] = 0;

        if (strncmp(input, "LIST", 4) == 0) {
            send_message(LIST);
            receive_and_save_files();
        } else if (strncmp(input, "DIFF", 4) == 0) {
            send_message(DIFF);
            receive_and_save_diff(&diff);
            printf("Missing files: %s", diff.data);
        } else if (strncmp(input, "PULL", 4) == 0) {
            send_message_pull(&diff);
            receive_and_save_files();
        } else if (strncmp(input, "LEAVE", 5) == 0) {
            send_message(LEAVE);
            return;
        } else {
            printf("Unknown command\n");
            continue;
        }
    }
}

void send_message_pull(Message* diff){
    diff->type = PULL;
    send(sock, diff, sizeof(Message), 0);
}

void receive_and_save_files(){
    Message file_descriptor;
    while(1) {
        memset(&file_descriptor, 0, sizeof(file_descriptor));
        if (recv(sock, &file_descriptor, sizeof(file_descriptor), 0) < 0) {
            perror("recv no work");
            return;
        }
        if (file_descriptor.data_length == 0){
            printf("End marker received.\n");
            break;
        }
        printf("File receiving - %s - with size %zu\n", file_descriptor.data, file_descriptor.data_length);
        int fd = open(file_descriptor.data, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("Failed to create file");
            return;
        }
        size_t total_received = 0, file_size = file_descriptor.data_length;
        while (total_received < file_size) {
            char buffer[BUFFER_SIZE];
            size_t to_read = (file_size - total_received < BUFFER_SIZE) ? file_size - total_received : BUFFER_SIZE;
            ssize_t bytes_read = recv(sock, buffer, to_read, 0);
            if (bytes_read <= 0) {
                perror("recv");
                return;
            }
            if (write(fd, buffer, bytes_read) < 0){
                perror("write");
                return;
            }
            total_received += bytes_read;
        }
        close(fd);
    }

    printf("All files received.\n");
}

int crawl_directory(char* data){
    DIR *dir;
    struct dirent *ent;

    dir = opendir(".");
    if (dir == NULL) {
        perror("Unable to open directory");
        return -1;
    }

    int index = 0, file_count = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) {  // Regular file
            // Add name and file size
            const char *file_name = ent->d_name;
            size_t name_length = strlen(file_name);

            if (name_length + index + 1 >= MAX_DATA_SIZE){
                perror("buffer exceeded too many files");
                return -1;
            }
            
            memcpy(&data[index], file_name, name_length);
            index += name_length;
            
            // Add delimiter |
            data[index] = '|';
            index++;
            file_count++;
        }
    }
    return file_count;
}

void receive_and_save_diff(Message* diff){
    if (recv(sock, diff, sizeof(Message), 0) < 0){
        perror("recv");
        return;
    }
}