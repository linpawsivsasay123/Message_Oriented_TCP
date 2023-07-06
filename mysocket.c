#include "mysocket.h"

int min (int a, int b) { return a < b ? a : b; }
int max (int a, int b) { return a > b ? a : b; }

table Send_Message, Received_Message;
int __n_sockfd;
pthread_t R_t, S_t;
pthread_mutex_t send_mutex, recv_mutex;
pthread_cond_t send_cond, recv_cond ;
pthread_attr_t attr;
int freed = 0;
int connection_closed;

/*
    Message format:
    1. 4 bytes for message size : 4 bytes for message size
    2. message of size max 5000 bytes
*/

void init()
{
    // other inits
    __n_sockfd = -1;
    connection_closed = 0;
    send_mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    recv_mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    send_cond = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    recv_cond = (pthread_cond_t) PTHREAD_COND_INITIALIZER;

    // allocate memory for Send_Message and Received_Message tables
    Send_Message.msg_table = (message *) malloc (MAX_TABLE_SIZE * sizeof(message));
    Received_Message.msg_table = (message *) malloc (MAX_TABLE_SIZE * sizeof(message));
    for (int i = 0; i < MAX_TABLE_SIZE; i++)
    {
        Send_Message.msg_table[i].size = 0;
        Received_Message.msg_table[i].size = 0;
        memset (Send_Message.msg_table[i].msg, 0, MAX_MSG_SIZE);
        memset (Received_Message.msg_table[i].msg, 0, MAX_MSG_SIZE);
    }
    Send_Message.idx = 0;
    Send_Message.size = 0;
    Received_Message.idx = 0;
    Received_Message.size = 0;

}

// ----- Thread R -----
/*
    It waits on a recv call on the TCP socket, receives  data  that  comes  in,  
    and  interpretes  the  data  to  form  the  message  (the complete  
    data for which may come in multiple recv calls), and puts the message in the 
    Received_Message table.
*/
void * thread_R (void * args)
{
    // buffer to store data received in recv calls
    char * buffer = (char *) malloc ((MAX_MSG_SIZE + 4)  * sizeof(char));
    memset (buffer, 0, (MAX_MSG_SIZE + 4));

    while (1)
    {
        if (__n_sockfd == -1) continue;

        // printf("Newsock: %d\n", __n_sockfd);
        fflush(stdout);

        // receive data = message length (4 bytes) + message (msg.size bytes)
        // recv length of message first
        int bytes_recved = 0;
        while (bytes_recved < 4 && !connection_closed)
        {
            // printf("i am in loop\n");
            int bytes = recv(__n_sockfd, buffer + bytes_recved, 4 - bytes_recved, 0);

            // if connection was closed 
            if (errno == EBADF)
            {
                printf("Connection closed\n");
                connection_closed = 1;
                break;
            }
            if (bytes == -1)
            {
                perror("recv error! 1");
                exit(1);
            }
            bytes_recved += bytes;
        }

        if (connection_closed) continue;

        // printf("Received %d bytes\n", bytes_recved);

        // get message length
        int msg_len = 0;
        for (int i = 0; i < 4; i++) msg_len += (buffer[i] - '0') * pow(10, 3 - i);
        
        // printf("Message length: %d\n", msg_len);
        // receive message
        bytes_recved = 0;
        while (bytes_recved < msg_len && !connection_closed)
        {
            int bytes = recv(__n_sockfd, buffer + bytes_recved + 4, msg_len - bytes_recved, 0);
            if (bytes == -1)
            {
                perror("recv error!");
                exit(1);
            }
            bytes_recved += bytes;
        }


        // lock mutex :: Start of critical Section
        pthread_mutex_lock(&recv_mutex);

        // wait on condition variable
        while (Received_Message.size == MAX_TABLE_SIZE) pthread_cond_wait(&recv_cond, &recv_mutex);

        // push message in Received_Message table
        int new_idx = (Received_Message.idx + Received_Message.size) % MAX_TABLE_SIZE;
        Received_Message.msg_table[new_idx].size = msg_len;
        for (int i = 0; i < msg_len; i++) Received_Message.msg_table[new_idx].msg[i] = buffer[i + 4];
        Received_Message.size++;
        // printf("R Size increased to %d\n", Received_Message.size);

        pthread_cond_signal(&recv_cond);

        // unlock mutex :: End of critical Section
        pthread_mutex_unlock(&recv_mutex);

        // printf("Entry in Received_Message table: %s\n", Received_Message.msg_table[new_idx].msg);
    }
}

// ----- Thread S -----
/*
    It sleeps for some time (T), and wakes up 
    periodically. On waking up, it sees if any message is waiting to be sent in the 
    Send_Message table. If so, it sends the message using one or more send calls on 
    the TCP socket. You can only send a maximum of 1000 bytes in a single send call. 
*/
void * thread_S (void * args)
{
    while (1)
    {        
        // lock mutex :: Start of critical Section
        pthread_mutex_lock(&send_mutex);

        // wait on condition variable
        while (Send_Message.size == 0) pthread_cond_wait(&send_cond, &send_mutex);

        // pop a message from Send_Message table
        message msg = Send_Message.msg_table[Send_Message.idx];
        Send_Message.idx = (Send_Message.idx + 1) % MAX_TABLE_SIZE;
        Send_Message.size--;
        // printf("Size decreased to %d\n", Send_Message.size);

        pthread_cond_signal(&send_cond);
        
        // unlock mutex :: End of critical Section
        pthread_mutex_unlock(&send_mutex);

        // send message length (4 bytes) + message (msg.size bytes)
        char * msg_body = (char *) malloc( (msg.size + 4) * sizeof(char) );
        int l = msg.size;
        for (int i = 0; i <= 3; i++)
        {
            msg_body[i] = l / (int) pow(10, 3 - i) + '0';
            l %= (int) pow(10, 3 - i); 
        }
        for (int i = 4; i < msg.size + 4; i++) msg_body[i] = msg.msg[i - 4];
        int msg_len = 4 + msg.size;

        // send message in packets of 1000 bytes 
        // (since max allowed size sendable in a single send call is 1000 bytes)
        int bytes_sent = 0;
        while (bytes_sent < msg_len)
        {
            int bytes_to_send = (msg_len - bytes_sent) > 1000 ? 1000 : (msg_len - bytes_sent);
            int send_status = send(__n_sockfd, msg_body + bytes_sent, bytes_to_send, 0);
            if (send_status < 0)
            {
                perror("Error sending message!");
                exit(1);
            }
            bytes_sent += send_status;
        }
    }
}


// my_socket() function
int my_socket (int domain, int type, int protocol)
{
    // create a TCP socket
    int sockfd = socket(domain, type, protocol);
    if (sockfd < 0)
    {
        perror("Error creating socket");
        exit(1);
    }

    // initialize Send_Message and Received_Message tables
    init();

    // create thread R
    pthread_attr_init(&attr);
    pthread_create(&R_t, &attr, thread_R, (void *) NULL);

    // create thread S
    pthread_create(&S_t, &attr, thread_S, (void *) NULL);

    return sockfd;
}

// my_bind() function
int my_bind (int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int bind_status = bind(sockfd, addr, addrlen);
    if (bind_status < 0)
    {
        perror("Error binding socket!");
        exit(1);
    }
    return bind_status;
}

// my_listen() function
int my_listen (int sockfd, int backlog)
{
    int listen_status = listen(sockfd, backlog);
    if (listen_status < 0)
    {
        perror("Error listening on socket!");
        exit(1);
    }
    return listen_status;
}

// my_accept() function
int my_accept (int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int newsock = accept(sockfd, addr, addrlen);
    if (newsock < 0)
    {
        perror("Error accepting connection!");
        exit(1);
    }
    __n_sockfd = newsock;
    // printf("Connection accepted! New socket created: %d\n", __n_sockfd);
    return newsock;
}

// my_connect() function
int my_connect (int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int connect_status = connect(sockfd, addr, addrlen);
    if (connect_status < 0)
    {
        perror("Error connecting to socket!");
        exit(1);
    }
    __n_sockfd = sockfd;
    return connect_status;
}

// my_send() function
ssize_t my_send (int sockfd, const void * buff, size_t len , int flags)
{
    message newmsg;

    // if message size > MAX_MSG_SIZE, send only MAX_MSG_SIZE bytes of message
    int sent_len = min(len, MAX_MSG_SIZE);
    for (int i = 0; i < sent_len; i++) newmsg.msg[i] = ((char *) buff)[i];
    newmsg.size = sent_len;

    // printf("newmsg = %s\n", newmsg.msg);

    // lock mutex :: Start of critical Section
    pthread_mutex_lock(&send_mutex);

    // wait on condition variable
    while (Send_Message.size == MAX_TABLE_SIZE) pthread_cond_wait(&send_cond, &send_mutex);

    // push message to Send_Message table
    int newidx = (Send_Message.idx + Send_Message.size) % MAX_TABLE_SIZE;
    Send_Message.msg_table[newidx] = newmsg;
    Send_Message.size++;

    // printf("Size updated to %d\n", Send_Message.size);

    // signal condition variable
    pthread_cond_signal(&send_cond);

    // unlock mutex :: End of critical Section
    pthread_mutex_unlock(&send_mutex);

    // return number of bytes sent
    return sent_len;
}

// my_recv() function
ssize_t my_recv (int sockfd, const void * buff, size_t len , int flags)
{   
    if (connection_closed) return 0;

    // lock mutex :: Start of critical Section
    pthread_mutex_lock(&recv_mutex);

    // wait on condition variable
    while (Received_Message.size == 0) pthread_cond_wait(&recv_cond, &recv_mutex);

    // pop message from Received_Message table
    message msg = Received_Message.msg_table[Received_Message.idx];
    Received_Message.idx = (Received_Message.idx + 1) % MAX_TABLE_SIZE;
    Received_Message.size--;

    // printf("R Size updated to %d\n", Received_Message.size);

    // signal condition variable
    pthread_cond_signal(&recv_cond);

    // unlock mutex :: End of critical Section
    pthread_mutex_unlock(&recv_mutex);

    int recv_len = min(len, msg.size);

    // copy message to buffer
    for (int i = 0; i < recv_len; i++) ((char *) buff)[i] = msg.msg[i];

    // return number of bytes received
    return recv_len;
}

// my_close() function
int my_close(int sockfd)
{
    // wait for 5 seconds
    sleep(5);
    int ret;

    // if new socket is being closed
    if (sockfd == __n_sockfd) return close(sockfd);
    else if (!freed) 
    {
        if (R_t != 0) pthread_cancel(R_t);
        if (S_t != 0) pthread_cancel(S_t);

        // free memory allocated for Send_Message and Received_Message tables
        if (Send_Message.msg_table != NULL) free(Send_Message.msg_table);
        if (Received_Message.msg_table != NULL) free(Received_Message.msg_table);

        // destroy mutex
        ret = pthread_mutex_destroy(&send_mutex);
        ret = pthread_mutex_destroy(&recv_mutex);

        // destroy condition variables
        ret = pthread_cond_destroy(&send_cond);
        ret = pthread_cond_destroy(&recv_cond);

        freed = 1;

        // close socket
        return close(sockfd);
    }
    else return 0;
}