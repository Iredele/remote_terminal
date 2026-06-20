/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   com_port.h
 */

#ifndef COM_PORT_H
#define COM_PORT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    COM_PORT_TYPE_UART,
    COM_PORT_TYPE_TCP,
} com_port_type_t;

typedef struct com_port_t
{
    bool     (*connect)(struct com_port_t *self);
    void     (*disconnect)(struct com_port_t *self);
    bool     (*is_connected)(struct com_port_t *self);
    bool     (*is_open)(struct com_port_t *self);
    uint32_t (*read)(struct com_port_t *self, uint8_t *buf, uint32_t len);
    uint32_t (*write)(struct com_port_t *self, const uint8_t *buf, uint32_t len);
    uint32_t (*bytes_available)(struct com_port_t *self);
    void     (*flush)(struct com_port_t *self);

    com_port_type_t type;
    bool            open;

    union
    {
        struct
        {
            uint32_t num;
        } uart;
        struct
        {
            uint16_t port;
            int      server_fd;
            int      client_fd;
        } tcp;
    };
} com_port_t;

void com_port_init(com_port_t *port, com_port_type_t type);
void com_port_set_num(com_port_t *port, uint16_t num);

#endif // COM_PORT_H
