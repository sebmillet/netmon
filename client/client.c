//
// client.c
//
// Sébastien Millet, April 2019
//

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define SOCKET_ERROR (-1)

#include <errno.h>
#include <string.h>

#define HOST  "localhost"
#define PORT  8080
#define SEND0 "GET / HTTP/1.1\015\012"

#define DEFAULT_CONNECT_TIMEOUT 5
#define DEFAULT_NETIO_TIMEOUT   10

void LOG(const char *format, ...) {
    va_list args;
    va_start(args, format);

    vprintf(format, args);
    printf("\n");

    va_end(args);
}

void FATAL_ERROR(const char *format, ...) {
    va_list args;
    va_start(args, format);

    vprintf(format, args);
    printf("\n");

    va_end(args);
    exit(EXIT_FAILURE);
}

void set_sock_nonblocking_mode(int sock) {
    long arg = fcntl(sock, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(sock, F_SETFL, arg);
}

static void set_sock_blocking_mode(int sock) {
    long arg = fcntl(sock, F_GETFL, NULL);
    arg &= ~O_NONBLOCK;
    fcntl(sock, F_SETFL, arg);
}

int main(int argc, char *argv[]) {

    const char *h = HOST;
    const int p = PORT;

        // Resolving server name
    struct sockaddr_in server;
    struct hostent *hostinfo = NULL;
    LOG("Running gethosbyname() on %s", h);
    hostinfo = gethostbyname(h);
    if (hostinfo == NULL) {
        FATAL_ERROR("Unknown host %s, %i: %s", h, errno, strerror(errno));
        return 5;
    }

    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == SOCKET_ERROR) {
        FATAL_ERROR("Socket creation error, %i: %s", errno, strerror(errno));
    }
    server.sin_family = AF_INET;
    server.sin_port = htons((uint16_t)p);
    server.sin_addr = *(struct in_addr *)hostinfo->h_addr;

    set_sock_nonblocking_mode(sock);

    int conn_to = DEFAULT_CONNECT_TIMEOUT;
    int netio_to = DEFAULT_NETIO_TIMEOUT;
    LOG("Connecting to %s:%i", h, p);

    if (!connect(sock, (struct sockaddr *)&server, sizeof(server))
            ||
        errno != EINPROGRESS) {
        FATAL_ERROR("Connection error (%s:%d), %i: %s", h, p,
                    errno, strerror(errno));
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET((unsigned int)(sock), &fdset);
    struct timeval conn_tv;
    conn_tv.tv_sec = conn_to;
    conn_tv.tv_usec = 0;
    if (select(sock + 1, NULL, &fdset, NULL, &conn_tv) <= 0) {
        FATAL_ERROR("Connection timeout (%s:%d), %i: %s", h, p,
                    errno, strerror(errno));
    }

    int so_error;
    socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
        FATAL_ERROR("Connection error (%s:%d), %i: %s", h, p,
                    so_error, strerror(so_error));
    }

    set_sock_blocking_mode(sock);

    struct timeval netio_tv;
    netio_tv.tv_sec = netio_to;
    netio_tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   (char *)&netio_tv, sizeof(netio_tv))) {
        FATAL_ERROR("Error setting network I/O timeout");
    }

    LOG("Connected to %s:%i", h, p);

    const char buffer[] = SEND0;
    const size_t buffer_len = sizeof(buffer);

    char *str = (char *)malloc(buffer_len + 1);
    memcpy(str, buffer, buffer_len);
    str[buffer_len] = '\0';
    LOG("Sending <%s> into the connection", str);
    free(str);

    return send(sock, buffer, buffer_len, 0);

    LOG("Now quitting, leaving connection as is");

    return 0;
}

