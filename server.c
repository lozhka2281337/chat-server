#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>

#define BACKLOG 15
#define PORT "8888"
#define SIZE_LOG_BUFFER 256
#define SIZE_OF_MESSAGE 1024

typedef struct client_info{
    int socket_fd;
    struct sockaddr_in addr;
    char client_ip[INET_ADDRSTRLEN];
    int is_active;
    time_t connected_at;
    time_t last_activity;
} client_info;

typedef enum log_level_t{
    LOG_INFO,
    LOG_ERROR
} log_level_t;

// write log
void log_message(log_level_t level, char *mes, ...){
    time_t now;
    time(&now);

    struct tm* local_time = localtime(&now);

    char buf_time[SIZE_LOG_BUFFER];
    strftime(buf_time, SIZE_LOG_BUFFER, "%d-%m-%Y %H-%M-%S", local_time);

    // choose log level 
    char *buf_level;
    switch (level){
        case LOG_INFO:  buf_level = "INFO"; break;
        case LOG_ERROR: buf_level = "ERROR"; break;
        default:        buf_level = "UNKNOWN"; break;
    }

    printf("[%s][%s] ", buf_time, buf_level);

    va_list args;
    va_start(args, mes);
    vfprintf(stdout, mes, args);
    va_end(args);

    printf("\n");
}

int create_server_socket(){
    int sockfd;
    struct addrinfo hints, *res, *p;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // get addresses
    int status_getaddrinfo; 
    if ((status_getaddrinfo = getaddrinfo(NULL, PORT, &hints, &res)) < 0){
        log_message(LOG_ERROR, "getaddrinfo: %s\n", gai_strerror(status_getaddrinfo));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next){
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            log_message(LOG_ERROR, "socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1){
            log_message(LOG_ERROR, "setsockopt");
            close(sockfd);
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            log_message(LOG_ERROR, "bind");
            close(sockfd);
            continue;
        }

        if (listen(sockfd, BACKLOG) == -1){
            log_message(LOG_ERROR, "listen");
            close(sockfd);
            continue;
        }

        break;
    }

    if (p == NULL) {
        log_message(LOG_ERROR, "server: failed to create");
        return -1;  
    }

    // not block socket
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    freeaddrinfo(res);
    return sockfd;
}

void new_client(int sockfd, fd_set *fds, int *max_sock, client_info **clients, int *capacity, int *count_clients){
    int newfd;
    struct sockaddr addr;
    socklen_t addrlen = sizeof addr;

    // process connection
    if ((newfd = accept(sockfd, &addr, &addrlen)) == -1){
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;

        log_message(LOG_ERROR, "accept: %s", strerror(errno));
        return;
    }

    // again not block socket
    fcntl(newfd, F_SETFL, O_NONBLOCK);

    // add new socket in set
    FD_SET(newfd, fds);
    *max_sock = (*max_sock < newfd) ? newfd : *max_sock;

    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in *sa = (struct sockaddr_in*)&addr;
    inet_ntop(AF_INET, &(sa->sin_addr), ip, INET_ADDRSTRLEN);

    // increase memory capacity 
    if (*capacity == *count_clients){
        *capacity *= 2;
        client_info *temp = realloc(*clients, *capacity * sizeof(client_info));
        if (temp == NULL){
            log_message(LOG_ERROR, "failed to add client");
            close(newfd);
            return;
        }

        *clients = temp;
    }

    time_t now;
    time(&now);

    // create client struct
    client_info client;
    client.socket_fd = newfd;
    client.addr = *sa;
    strcpy(client.client_ip, ip);
    client.connected_at = now;
    client.last_activity = now;
    client.is_active = 1;

    (*clients)[(*count_clients)++] = client;

    log_message(LOG_INFO, "new connection from %s: number %d; socket: %d", client.client_ip, *count_clients, client.socket_fd);
}

// send message all clients (without sender)
void send_all(int sockfd, const char *buf, ssize_t *len){
    ssize_t sent_bytes = 0;

    while (sent_bytes < *len){
        ssize_t sent = send(sockfd, buf+sent_bytes, *len-sent_bytes, 0);
        if (sent == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        
        sent_bytes += sent;
    }

    *len = sent_bytes;
}

void delete_client(int client_sock, fd_set *fds, client_info *clients, int *count_clients){
    close(client_sock);
    FD_CLR(client_sock, fds);

    for (int i = 0; i < *count_clients; i++){
        if (clients[i].socket_fd == client_sock){
            if (i != *count_clients-1) clients[i] = clients[*count_clients-1];
            (*count_clients)--;
        }
    }
}

void handle_client_data(int client_sock, fd_set *fds, client_info *clients, int *count_clients){
    char buf[SIZE_OF_MESSAGE];

    ssize_t read_bytes;
    // read data
    if ((read_bytes = recv(client_sock, buf, SIZE_OF_MESSAGE, 0)) <= 0){
        log_message(LOG_ERROR, "client passed out");
        delete_client(client_sock, fds, clients, count_clients);
        return;
    }

    // send data to clients
    for (int s = 0; s < *count_clients; s++){
        client_info client = clients[s];

        if (client.socket_fd == client_sock) {
            clients[s].last_activity = time(NULL);
            continue;
        }

        ssize_t len = read_bytes;
        
        send_all(client.socket_fd, buf, &len);
        if (len != read_bytes){
            log_message(LOG_ERROR, "failed to send full message: %ld/%ld bytes", len, read_bytes);
        } 
    }
}

// close server
void completion_server(int sock_fd, client_info *clients, int count_clients, fd_set *master){
    for (int cl = 0; cl < count_clients; cl++){
        client_info client = clients[cl];
        close(client.socket_fd);
    }

    FD_ZERO(master);
    free(clients);
    close(sock_fd);

    exit(0);
}

void config_timeout(int argc, char *argv[], int *timeout){
    if (argc > 2) {
        char *config_t = argv[1];
        if (!strcmp(config_t, "-timeout")){
            const char* new_timeout = (const char *)argv[2];

            int i = 0;
            while (*new_timeout){
                if (!isdigit(*new_timeout)) return;
                new_timeout++;
                i++;
            }

            *timeout = atoi(new_timeout-i);            
        } else log_message(LOG_INFO, "incorrect arguments");
    }
}

int main(int argc, char *argv[]){
    int timeout = 30;
    config_timeout(argc, argv, &timeout);

    int sockfd, max_sock;
    fd_set master, readfds;

    // clients array
    int capacity = 100, count_clients = 0;
    client_info *clients = malloc(sizeof(client_info) * capacity);
    if (clients == NULL){
        log_message(LOG_ERROR, "failed to malloc");
        exit(EXIT_FAILURE);
    }

    sockfd = create_server_socket();
    if (sockfd == -1) return 1;
    max_sock = sockfd;

    FD_ZERO(&readfds);
    FD_ZERO(&master);

    FD_SET(sockfd, &master);

    log_message(LOG_INFO ,"server is waiting connection with timeout %d s...", timeout);
    for (;;){
        fd_set readfds = master;
        struct timeval tm = {timeout, 0};

        if (select(max_sock+1, &readfds, NULL, NULL, &tm) == -1){
            log_message(LOG_ERROR, "server failed to select");
            completion_server(sockfd, clients, count_clients, &master);
        }
        
        // new connection
        if (FD_ISSET(sockfd, &readfds)) new_client(sockfd, &master, &max_sock, &clients, &capacity, &count_clients);

        // new message
        for (int i = 0; i < count_clients; i++){
            if (FD_ISSET(clients[i].socket_fd, &readfds)) {
                handle_client_data(clients[i].socket_fd, &master, clients, &count_clients);
                continue;
            }

            time_t now = time(NULL);
            clients[i].is_active = !((now - clients[i].last_activity) > timeout);

            if (!clients[i].is_active){
                log_message(LOG_INFO, "bye-bye, socket %d", clients[i].socket_fd);
                delete_client(clients[i].socket_fd, &master, clients, &count_clients);
                i--;
            }
        }
    }

    completion_server(sockfd, clients, count_clients, &master);

    return 0;
}