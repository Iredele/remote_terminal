/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   tcp.c
 */

#include "tcp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_log.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

static const char *TAG = "tcp";

tcp_socket_t TCPOpenServer(uint16_t port)
{
    int sockfd;
    int opt;
    struct sockaddr_in addr;
    int len;
    int err;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        ESP_LOGE(TAG, "socket failed");
        return TCP_INVALID_SOCKET;
    }

    addr.sin_addr.s_addr = htonl(IPADDR_ANY);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    len = sizeof(addr);

    opt = 1;
    err = setsockopt(sockfd, SOL_SOCKET,
                    SO_REUSEADDR, &opt,
                    sizeof(opt));
    if (err < 0)
    {
        ESP_LOGE(TAG, "setsockopt failed");
        close(sockfd);
        return TCP_INVALID_SOCKET;
    }

    err = bind(sockfd, (struct sockaddr*)&addr, len);
    if (err < 0)
    {
        ESP_LOGE(TAG, "bind failed");
        close(sockfd);
        return TCP_INVALID_SOCKET;
    }

    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0)
    {
        ESP_LOGE(TAG, "could not read sock flags");
        close(sockfd);
        return TCP_INVALID_SOCKET;
    }
    fcntl(sockfd, F_SETFL, flags|O_NONBLOCK);

    err = listen(sockfd, 10);
    if (err < 0)
    {
        ESP_LOGE(TAG, "listen failed");
        close(sockfd);
        return TCP_INVALID_SOCKET;
    }

    ESP_LOGI(TAG, "listening on port %d", port);
    return (tcp_socket_t)sockfd;
}

tcp_socket_t TCPAccept(tcp_socket_t server)
{
    int                 connfd;
    struct sockaddr_in  addr;
    socklen_t           addr_len;

    if (server == TCP_INVALID_SOCKET)
    {
        return TCP_INVALID_SOCKET;
    }

    addr_len = sizeof(addr);
    connfd = accept(server, (struct sockaddr*)&addr, &addr_len);
    if (connfd < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            ESP_LOGE(TAG, "accept failed: errno %d", errno);
        return TCP_INVALID_SOCKET;
    }

    ESP_LOGI(TAG, "connection accepted");
    return (tcp_socket_t)connfd;
}

void TCPClose(tcp_socket_t sock)
{
    close(sock);
}

bool TCPIsConnected(tcp_socket_t sock)
{
    int n;
    fd_set rset;

    if (sock == TCP_INVALID_SOCKET)
    {
        return false;
    }

    FD_ZERO(&rset);
    FD_SET(sock, &rset);

    struct timeval tv = {0, 0};
    n = select(sock + 1, &rset, NULL, NULL, &tv);
    if (n < 0)
    {
        return false;
    }
    if (n == 0)
    {
        return true;
    }

    uint8_t b;
    int peek = recv(sock, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (peek > 0)
    {
        return true;
    }
    if (peek < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        return true;
    }

    return false;
}

uint32_t TCPWrite(tcp_socket_t sock, const uint8_t *data, uint32_t len)
{
    int wlen;
    uint32_t total;

    if (sock == TCP_INVALID_SOCKET || data == NULL || len == 0)
    {
        return 0;
    }

    total = 0;
    while (total < len)
    {
        wlen = send(sock, data + total, len - total, 0);
        if (wlen > 0)
        {
            total += (uint32_t)wlen;
        }
        if (wlen < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
        {
            break;
        }
        if (wlen < 0 && errno == EINTR)
        {
            break;
        }
        if (wlen <= 0)
        {
            break;
        }
    }

    return total;
}

uint32_t TCPWriteString(tcp_socket_t sock, const char *str)
{
    if (sock == TCP_INVALID_SOCKET)
    {
        return 0;
    }

    uint32_t len = strlen(str);
    if (len == 0)
    {
        return 0;
    }

    return TCPWrite(sock, (const uint8_t*)str, len);
}

uint32_t TCPRead(tcp_socket_t sock, uint8_t *buff, uint32_t len)
{
    int rlen;

    if (sock == TCP_INVALID_SOCKET || buff == NULL || len == 0)
    {
        return 0;
    }

    rlen = recv(sock, buff, len, MSG_DONTWAIT);

    return (rlen < 0) ? 0 : (uint32_t)rlen;
}

uint32_t TCPBytesAvailable(tcp_socket_t sock)
{
    if (sock == TCP_INVALID_SOCKET)
        return 0;

    uint8_t peek_buf[1024];
    int r;
    while (1)
    {
        r = recv(sock, peek_buf, sizeof(peek_buf), MSG_PEEK | MSG_DONTWAIT);
        if (r >= 0 || errno != EINTR)
            break;
    }
    return (r < 0) ? 0 : (uint32_t)r;
}

uint32_t TCPPeek(tcp_socket_t sock, uint8_t *buff, uint32_t len)
{
    int rlen;
    if (sock == TCP_INVALID_SOCKET || buff == NULL || len == 0)
    {
        return 0;
    }

    while (1)
    {
        rlen = recv(sock, buff, len, MSG_PEEK|MSG_DONTWAIT);
        if (rlen >= 0 || errno != EINTR)
        {
            break;
        }
    }

    return (rlen < 0) ? 0 : (uint32_t)rlen;
}

void TCPDiscard(tcp_socket_t sock, uint32_t len)
{
    int         rlen;
    uint32_t    remaining;
    uint8_t     buff[128];

    remaining = len;
    while (remaining > 0)
    {
        uint32_t chunk = (remaining > sizeof(buff)) ? sizeof(buff) : remaining;
        rlen = recv(sock, buff, chunk, MSG_DONTWAIT);
        if (rlen <= 0)
        {
            break;
        }
        remaining -= (uint32_t)rlen;
    }
}