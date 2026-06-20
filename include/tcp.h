/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   tcp.h
 */

#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <stdbool.h>

#define TCP_INVALID_SOCKET  (-1)
typedef int tcp_socket_t;

tcp_socket_t    TCPOpenServer(uint16_t port);
tcp_socket_t    TCPAccept(tcp_socket_t server);
void            TCPClose(tcp_socket_t sock);

bool            TCPIsConnected(tcp_socket_t sock);

uint32_t        TCPWrite(tcp_socket_t sock, const uint8_t *data, uint32_t len);
uint32_t        TCPWriteString(tcp_socket_t sock, const char *str);


uint32_t        TCPRead(tcp_socket_t sock, uint8_t *buff, uint32_t len);
uint32_t        TCPBytesAvailable(tcp_socket_t sock);
uint32_t        TCPPeek(tcp_socket_t sock, uint8_t *buff, uint32_t len);
void            TCPDiscard(tcp_socket_t sock, uint32_t len);

#endif // TCP_H
