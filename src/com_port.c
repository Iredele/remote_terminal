/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   com_port.c
 */

#include "com_port.h"
#include "tcp.h"
#include "uart32.h"

#define UART_PORT_MAX (3)

static bool com_port_connect (com_port_t *self)
{
   if (self->open)
   {
      return true;
   }

   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
      {
         self->open = true;
         return self->open;
      }

      case COM_PORT_TYPE_TCP:
      {
         if (self->tcp.server_fd == TCP_INVALID_SOCKET)
         {
            self->tcp.server_fd = TCPOpenServer (self->tcp.port);
            if (self->tcp.server_fd == TCP_INVALID_SOCKET)
            {
               return false;
            }
         }
         self->tcp.client_fd = TCPAccept (self->tcp.server_fd);
         self->open          = (self->tcp.client_fd != TCP_INVALID_SOCKET);
         return self->open;
      }
   }

   return false;
}

static void com_port_disconnect (com_port_t *self)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
      {
         UARTFlush (self->uart.num);
         self->open = false;
         break;
      }

      case COM_PORT_TYPE_TCP:
      {
         if (self->tcp.client_fd != TCP_INVALID_SOCKET)
         {
            TCPClose (self->tcp.client_fd);
            self->tcp.client_fd = TCP_INVALID_SOCKET;
         }
         self->open = false;
         break;
      }
   }
}

static bool com_port_is_connected (com_port_t *self)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
         return self->open;

      case COM_PORT_TYPE_TCP:
         return (self->tcp.client_fd != TCP_INVALID_SOCKET)
                && TCPIsConnected (self->tcp.client_fd);
   }

   return false;
}

static bool com_port_is_open (com_port_t *self)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
         return self->open;

      case COM_PORT_TYPE_TCP:
         return self->tcp.server_fd != TCP_INVALID_SOCKET;
   }

   return false;
}

static uint32_t com_port_read (com_port_t *self, uint8_t *buf, uint32_t len)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
         return UARTRead (self->uart.num, buf, len);

      case COM_PORT_TYPE_TCP:
         if (self->tcp.client_fd == TCP_INVALID_SOCKET)
         {
            return 0;
         }
         return TCPRead (self->tcp.client_fd, buf, len);
   }

   return 0;
}

static uint32_t com_port_write (com_port_t *self, const uint8_t *buf,
                                uint32_t len)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
         return UARTWrite (self->uart.num, buf, len);

      case COM_PORT_TYPE_TCP:
         if (self->tcp.client_fd == TCP_INVALID_SOCKET)
         {
            return 0;
         }
         return TCPWrite (self->tcp.client_fd, buf, len);
   }

   return 0;
}

static uint32_t com_port_bytes_available (com_port_t *self)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
         return UARTBytesAvailable (self->uart.num);

      case COM_PORT_TYPE_TCP:
         if (self->tcp.client_fd == TCP_INVALID_SOCKET)
         {
            return 0;
         }
         return TCPBytesAvailable (self->tcp.client_fd);
   }

   return 0;
}

static void com_port_flush (com_port_t *self)
{
   switch (self->type)
   {
      case COM_PORT_TYPE_UART:
      {
         UARTFlush (self->uart.num);
         break;
      }

      case COM_PORT_TYPE_TCP:
      {
         if (self->tcp.client_fd != TCP_INVALID_SOCKET)
         {
            uint32_t avail = TCPBytesAvailable (self->tcp.client_fd);
            if (avail > 0)
            {
               TCPDiscard (self->tcp.client_fd, avail);
            }
         }
         break;
      }
   }
}

void com_port_set_num (com_port_t *port, uint16_t num)
{
   switch (port->type)
   {
      case COM_PORT_TYPE_UART:
         if (num >= UART_PORT_MAX)
         {
            return;
         }
         port->uart.num = num;
         break;

      case COM_PORT_TYPE_TCP:
         port->tcp.port = num;
         break;
   }
}

void com_port_init (com_port_t *port, com_port_type_t type)
{
   port->connect         = com_port_connect;
   port->disconnect      = com_port_disconnect;
   port->is_connected    = com_port_is_connected;
   port->is_open         = com_port_is_open;
   port->read            = com_port_read;
   port->write           = com_port_write;
   port->bytes_available = com_port_bytes_available;
   port->flush           = com_port_flush;

   port->type = type;
   port->open = false;

   if (type == COM_PORT_TYPE_TCP)
   {
      port->tcp.server_fd = TCP_INVALID_SOCKET;
      port->tcp.client_fd = TCP_INVALID_SOCKET;
   }
}
