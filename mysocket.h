#ifndef _MY_SOCKET_H
#define _MY_SOCKET_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>

#define MAX_MSG_SIZE 5000
#define MAX_TABLE_SIZE 10
#define SOCK_MyTCP SOCK_STREAM
#define T_SLEEP 2

typedef struct
{
    int size;
    char msg[MAX_MSG_SIZE];
} message;

typedef struct
{
    // idx : index of the first filled slot
    int idx;  
    int size;
    message * msg_table;
} table;


int my_socket(int domain, int type, int protocol);
int my_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int my_listen(int sockfd, int backlog);
int my_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int my_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t my_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t my_recv(int sockfd, const void *buf, size_t len, int flags);
int my_close(int fd);

#endif