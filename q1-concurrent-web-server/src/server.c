/*
    C socket server example, handles multiple clients using threads
*/
 
#include<stdio.h>
#include<string.h>    //strlen
#include<stdlib.h>
#include<sys/socket.h>
#include<arpa/inet.h> //inet_addr
#include<unistd.h>    //write
#include<pthread.h> //for threading , link with lpthread
#include <errno.h>
#include "server.h"


size_t rio_writen(int fd, const char *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nwritten;
    const char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nwritten = write(fd, bufp, nleft)) <= 0) {
            return 0;
        }
        nleft -= nwritten;
        bufp += nwritten;
    }

    return n;
}
int parse_request(const char *req_str, request_t *req_info) {
    if (sscanf(req_str, "%7s %511s %15[^\r\n]",
               req_info->method,
               req_info->uri,
               req_info->version) != 3) {
        fprintf(stderr, "malformed http request\n");
        return -1;
    }

    printf("method %s uri %s\n",req_info->method, req_info->uri);

    if (strcmp(req_info->method, "GET") != 0) {
        fprintf(stderr, "unsupported method\n");
        return -1;
    }

    return 0;
}

FILE *handle_request(const request_t *req)
{
    char path[1024];

    if (strcmp(req->uri, "/") == 0) {
        snprintf(path, sizeof(path), "www/index.html");
    } else {
        snprintf(path, sizeof(path), "www/%s", req->uri + 1);
    }

    printf("requested path: %s\n", path);
    return fopen(path, "rb");
}

void send_file_response(int connfd, FILE *file)
{
    long file_size;
    char *buffer;

    fseek(file, 0L, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);

    if (file_size < 0) {
        fclose(file);
        send_response(connfd, ISE, content_500, strlen(content_500));
        return;
    }

    if (file_size == 0) {
        fclose(file);
        send_response(connfd, OK, "", 0);
        return;
    }

    buffer = malloc(file_size);
    if (buffer == NULL) {
        fclose(file);
        send_response(connfd, ISE, content_500, strlen(content_500));
        return;
    }

    if (fread(buffer, 1, file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        send_response(connfd, ISE, content_500, strlen(content_500));
        return;
    }

    send_response(connfd, OK, buffer, file_size);

    free(buffer);
    fclose(file);
}



void send_response(int connfd, status_t status,
                   const char *content, size_t content_length)
{
    char buf[256];
    int written;

    if (status == NF) {
        written = snprintf(buf, sizeof(buf),
                           "HTTP/1.0 404 Not Found\r\n"
                           "Content-Length: %zu\r\n\r\n",
                           content_length);
    } else if (status == OK) {
        written = snprintf(buf, sizeof(buf),
                           "HTTP/1.0 200 OK\r\n"
                           "Content-Length: %zu\r\n\r\n",
                           content_length);
    } else {
        written = snprintf(buf, sizeof(buf),
                           "HTTP/1.0 500 Internal Server Error\r\n"
                           "Content-Length: %zu\r\n\r\n",
                           content_length);
    }

    if (written < 0 || (size_t)written >= sizeof(buf)) {
        fprintf(stderr, "error while building response header\n");
        return;
    }

    if (rio_writen(connfd, buf, (size_t)written) < (size_t)written) {
        fprintf(stderr, "error while sending response header\n");
        return;
    }

    if (content_length > 0 &&
        rio_writen(connfd, content, content_length) < content_length) {
        fprintf(stderr, "error while sending response body\n");
    }
}


//the thread function
void *connection_handler(void *);

int main(void)
{
    int socket_desc, client_sock, rc, c, *new_sock;
    struct sockaddr_in server, client;
    int on = 1;

    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        perror("Could not create socket");
        return 1;
    }
    puts("Socket created");

    rc = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    if (rc < 0)
    {
        perror("setsockopt() failed");
        close(socket_desc);
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(3000);

    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("bind failed");
        close(socket_desc);
        return 1;
    }
    puts("bind done");

    if (listen(socket_desc, 3) < 0) {
        perror("listen failed");
        close(socket_desc);
        return 1;
    }

    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);

    while (1) {
        client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c);
        if (client_sock < 0) {
            perror("accept failed");
            continue;
        }

        puts("Connection accepted");

        pthread_t sniffer_thread;
        new_sock = malloc(sizeof(int));
        if (new_sock == NULL) {
            perror("malloc failed");
            close(client_sock);
            continue;
        }

        *new_sock = client_sock;

        if (pthread_create(&sniffer_thread, NULL, connection_handler, (void *)new_sock) != 0) {
            perror("could not create thread");
            close(client_sock);
            free(new_sock);
            continue;
        }

        pthread_detach(sniffer_thread);
        puts("Handler assigned");
    }

    close(socket_desc);
    return 0;
}


/*
 * This will handle connection for each client
 * */
void *connection_handler(void *socket_desc)
{
    //Get the socket descriptor
    int sock = *(int *)socket_desc;
    free(socket_desc);
    socket_desc = NULL;
    int sz = -1;
    char data[2001];
     
    //Receive a message from client
    while((sz = recv(sock, data, 2000, 0)) > 0)
    {
        data[sz] = '\0';

        //readn++;
        if (sz >= 4
            && data[sz - 1] == '\n'
            && data[sz - 2] == '\r'
            && data[sz - 3] == '\n'
            && data[sz - 4] == '\r' ){ 
            
            printf("client message %s\n",data);
            request_t req_info;
            if (parse_request(data, &req_info) < 0) {
                //error
                send_response(sock, ISE, content_500, strlen(content_500));
                break;
            }

            FILE *file = handle_request(&req_info);
            if (file == NULL) {
                send_response(sock, NF, content_404, strlen(content_404));
                break;
            }

            send_file_response(sock, file);
            break;
        }
    }
     
    if(sz == 0){
        puts("Client disconnected");
        fflush(stdout);
    } else if (sz == -1){
        perror("recv failed");
    }
         
    close(sock);     
    return NULL;
}
