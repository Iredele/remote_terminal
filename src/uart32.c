/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   uart32.c
 */

#include "uart32.h"

#include "esp_log.h"
#include "driver/uart.h"

#include <string.h>

#define UART_PORTS_MAX  (3u)

static QueueHandle_t uart_queue[UART_PORTS_MAX];

uart32_err_t UARTInit(uint32_t port, const uart32_cfg_t *config)
{
    uart_config_t uart_config = {
        .baud_rate              = config->baud_rate,
        .data_bits              = config->data_bits,
        .parity                 = config->parity,
        .stop_bits              = config->stop_bits,
        .flow_ctrl              = config->flow_ctrl,
        .rx_flow_ctrl_thresh    = 122,
    };

    esp_err_t err = uart_param_config(port, &uart_config);
    if (err != ESP_OK) 
    {
        return (UART_ERR_INIT);
    }

    // set UART pins(TX: IO4, RX: IO5, RTS: IO18, CTS: IO19, DTR: UNUSED, DSR: UNUSED)
    err = uart_set_pin(port, 
                       config->tx_pin,
                       config->rx_pin,
                       config->rts_pin,
                       config->cts_pin);
    if (err != ESP_OK) 
    {
        return (UART_ERR_INIT);
    }
    // install UART driver using an event queue here
    err = uart_driver_install(port, config->buffer_size, config->buffer_size, 10, &uart_queue[port], 0);
    if (err != ESP_OK) 
    {
        return (UART_ERR_INIT);
    }

    return (UART_OK);
}

uint32_t UARTWrite(uint32_t port, const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0)
        return 0;

    int sent = uart_write_bytes(port, (const char *)data, len);

    if (sent < 0)
    {
        ESP_LOGE("uart32", "uart_write_bytes failed on port %lu, requested %lu bytes",
                 (unsigned long)port, (unsigned long)len);
        return 0;
    }

    if (sent < (int)len)
    {
        ESP_LOGW("uart32", "uart_write_bytes partial write: sent %d/%lu bytes",
                 sent, (unsigned long)len);
    }

    return (uint32_t)sent;
}

uint32_t UARTWriteString(uint32_t port, const char *str)
{
    if (str == NULL)
        return 0;

    return UARTWrite(port, (const uint8_t *)str, strlen(str));
}

uint32_t UARTRead(uint32_t port, uint8_t *buff, uint32_t len)
{
    if (buff == NULL || len == 0)
        return 0;

    int rlen = uart_read_bytes(port, buff, len, 0);

    if (rlen < 0)
    {
        ESP_LOGW("uart32", "uart_read_bytes failed on port %lu", (unsigned long)port);
        return 0;
    }

    return (uint32_t)rlen;
}

uint32_t UARTBytesAvailable(uint32_t port)
{
    size_t length = 0;
    uart_get_buffered_data_len(port, &length);

    return (uint32_t)length;
}

void UARTFlush(uint32_t port)
{
    uart_wait_tx_done(port, pdMS_TO_TICKS(500));
    uart_flush_input(port);
}