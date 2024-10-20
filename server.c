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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#define MAX_CLIENTS 10
#define MAX_DATA_SIZE 4096
#define MAX_FILES 32

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

typedef struct {
    char filename[256];
    char hash[SHA256_DIGEST_LENGTH * 2 + 1];
} FileInfo;

void *handle_client(void *arg);
void handle_list(int client_socket);
void handle_diff(int client_socket, char *client_files);
void handle_pull(int client_socket, const char *filename);
void handle_leave(int client_socket);
void calculate_file_hash(const char *filename, char *hash);

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
    address.sin_port = htons(9090);

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
    DIR *dir;
    struct dirent *ent;
    Message file_descriptor;

    // Send file contents
    dir = opendir(".");
    if (dir == NULL) {
        perror("Unable to open directory");
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG) {  // Regular file
            int fd = open(ent->d_name, O_RDONLY);
            if (fd == -1) {
                printf("Unable to open file %s\n", ent->d_name);
                continue;
            }

            struct stat st;
            if (fstat(fd, &st) == -1){
                perror("fstat failed");
                close(fd);
                continue;
            }
            // Send name and file size first
            memset(&file_descriptor, 0, sizeof(file_descriptor));

            const char *file_name = ent->d_name;
            size_t name_length = strlen(file_name);
            memcpy(&file_descriptor.data, file_name, name_length);
            file_descriptor.data_length = st.st_size;
            send(client_socket, &file_descriptor, sizeof(file_descriptor), 0);

            printf("File sending - %s - with size %zu\n", file_descriptor.data, file_descriptor.data_length);
            // Use sendfile to send file contents
            ssize_t sent_bytes = sendfile(client_socket, fd, NULL, st.st_size);
            if (sent_bytes == -1) {
                perror("sendfile");
            } else {
                printf("Sent %zd bytes for file %s\n", sent_bytes, ent->d_name);
            }

            close(fd);
        }
    }

    closedir(dir);

    // Send end of transmission marker
    memset(&file_descriptor, 0, sizeof(file_descriptor));
    send(client_socket, &file_descriptor, sizeof(file_descriptor), 0);

    printf("LIST for client concluded\n");
}

void handle_diff(int client_socket, char *client_data) {
    DIR *dir;
    struct dirent *ent;
    FileInfo server_files[MAX_FILES];
    FileInfo missing_files[MAX_FILES];
    int server_file_count = 0;
    int missing_file_count = 0;
    
    // Get list of files on the server with their hashes
    dir = opendir(".");
    if (dir == NULL) {
        perror("Unable to open directory");
        return;
    }

    while ((ent = readdir(dir)) != NULL && server_file_count < MAX_FILES) {
        if (ent->d_type == DT_REG) {  // Regular file
            strncpy(server_files[server_file_count].filename, ent->d_name, sizeof(server_files[server_file_count].filename) - 1);
            calculate_file_hash(ent->d_name, server_files[server_file_count].hash);
            server_file_count++;
        }
    }
    closedir(dir);

    // Compare server files with client files
    FileInfo *client_files = (FileInfo *)client_data;
    int client_file_count = ((Message *)client_data)->data_length / sizeof(FileInfo);

    for (int i = 0; i < server_file_count; i++) {
        int found = 0;
        for (int j = 0; j < client_file_count; j++) {
            if (strcmp(server_files[i].filename, client_files[j].filename) == 0) {
                found = 1;
                if (strcmp(server_files[i].hash, client_files[j].hash) != 0) {
                    // File exists but has different hash
                    memcpy(&missing_files[missing_file_count], &server_files[i], sizeof(FileInfo));
                    missing_file_count++;
                }
                break;
            }
        }
        if (!found) {
            // File doesn't exist on client
            memcpy(&missing_files[missing_file_count], &server_files[i], sizeof(FileInfo));
            missing_file_count++;
        }
    }

    // Prepare and send response
    Message response;
    memcpy(response.data, missing_files, missing_file_count * sizeof(FileInfo));
    response.data_length = missing_file_count * sizeof(FileInfo);
    response.type = DIFF;
    send(client_socket, &response, sizeof(response), 0);

    printf("Sent difference information to client.\n");
}

void handle_pull(int client_socket, const char *filenames) {
    DIR *dir;
    struct dirent *ent;
    Message file_descriptor;

    // Send file contents
    dir = opendir(".");
    if (dir == NULL) {
        perror("Unable to open directory");
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_REG && strstr(filenames, ent->d_name) != NULL) {  // Regular file
            int fd = open(ent->d_name, O_RDONLY);
            if (fd == -1) {
                printf("Unable to open file %s\n", ent->d_name);
                continue;
            }

            struct stat st;
            if (fstat(fd, &st) == -1){
                perror("fstat failed");
                close(fd);
                continue;
            }
            // Send name and file size first
            memset(&file_descriptor, 0, sizeof(file_descriptor));

            const char *file_name = ent->d_name;
            size_t name_length = strlen(file_name);
            memcpy(&file_descriptor.data, file_name, name_length);
            file_descriptor.data_length = st.st_size;
            send(client_socket, &file_descriptor, sizeof(file_descriptor), 0);

            printf("File sending - %s - with size %zu\n", file_descriptor.data, file_descriptor.data_length);

            ssize_t sent_bytes = sendfile(client_socket, fd, NULL, st.st_size);
            if (sent_bytes == -1) {
                perror("sendfile");
            } else {
                printf("Sent %zd bytes for file %s\n", sent_bytes, ent->d_name);
            }

            close(fd);
        }
    }

    closedir(dir);

    // Send end of transmission marker
    memset(&file_descriptor, 0, sizeof(file_descriptor));
    send(client_socket, &file_descriptor, sizeof(file_descriptor), 0);

    printf("PULL for client concluded\n");
}

void handle_leave(int client_socket) {
    printf("Handling LEAVE request\n");
    Message response = {LEAVE, 7, "Goodbye"};
    send(client_socket, &response, sizeof(Message), 0);
}

void calculate_file_hash(const char *filename, char *hash) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    unsigned char buffer[1024];
    int bytesRead = 0;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        SHA256_Update(&sha256, buffer, bytesRead);
    }

    unsigned char hash_result[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash_result, &sha256);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(&hash[i * 2], "%02x", hash_result[i]);
    }

    fclose(file);
}