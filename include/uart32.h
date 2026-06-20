/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   uart32.h
 */

#ifndef UART32_H
#define UART32_H

#include <stdint.h>
#include <stdbool.h>

#include "driver/uart.h"

#define UNUSED_PIN (-1)

typedef enum {
    UART_OK = 0,
    UART_ERR_INIT,
    UART_ERR_INVALID_ARG,
} uart32_err_t;

typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    int tx_pin;
    int rx_pin;
    int rts_pin;
    int cts_pin;
    uint32_t buffer_size;
} uart32_cfg_t;

uart32_err_t  UARTInit(uint32_t port, const uart32_cfg_t *config);
uint32_t    UARTWrite(uint32_t port, const uint8_t *data, uint32_t len);
uint32_t    UARTWriteString(uint32_t port, const char *str);
uint32_t    UARTRead(uint32_t port, uint8_t *buff, uint32_t len);
uint32_t    UARTBytesAvailable(uint32_t port);
void        UARTFlush(uint32_t port);

#endif // UART32_H