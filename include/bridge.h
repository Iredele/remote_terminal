/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   bridge.h
 */

#ifndef BRIDGE_H
#define BRIDGE_H

#include "com_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
   BRIDGE_STATE_INIT,
   BRIDGE_STATE_WAIT,
   BRIDGE_STATE_CONNECT,
   BRIDGE_STATE_SEND,
} bridge_state_t;

typedef struct
{
   com_port_t    *port_a;
   com_port_t    *port_b;
   uint16_t       idle_timeout;
   uint32_t       _idle_ticks;
   bool           is_running;
   bool           is_open;
   bridge_state_t state;
} bridge_t;

void bridge_init (bridge_t *bridge, com_port_t *port_a, com_port_t *port_b);
void bridge_set_ports (bridge_t *bridge, com_port_t *port_a,
                       com_port_t *port_b);
void bridge_set_idle_timeout (bridge_t *bridge, uint16_t idle_timeout);
void bridge_start (bridge_t *bridge);
void bridge_stop (bridge_t *bridge);

bool bridge_is_running (bridge_t *bridge);
bool bridge_is_open (bridge_t *bridge);

void bridge_manager_init (bridge_t **bridges, uint16_t count);
void bridge_manager_start (void);

#endif // BRIDGE_H
