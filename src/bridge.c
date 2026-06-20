/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   bridge.c
 */

#include "bridge.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "bridge";

#define BRIDGE_BUFFER_SZ    (1024u)
#define PORT_NAME(p)        ((p)->type == COM_PORT_TYPE_TCP ? "TCP" : "UART")
#define BRIDGE_TASK_STACK   (4096u)
#define BRIDGE_TASK_PRIO    (5u)
#define BRIDGE_TICK_MS      (10u)

static bridge_t   **_bridges        = NULL;
static uint16_t     _bridge_count   = 0;
static TaskHandle_t _task_handle    = NULL;


void bridge_init(bridge_t *bridge, com_port_t *port_a, com_port_t *port_b)
{
    if (bridge == NULL || port_a == NULL || port_b == NULL)
    {
        return;
    }
    bridge->port_a          = port_a;
    bridge->port_b          = port_b;
    bridge->idle_timeout    = 10;
    bridge->_idle_ticks     = 0;
    bridge->is_open         = false;
    bridge->is_running      = false;
    bridge->state           = BRIDGE_STATE_INIT;
}

void bridge_set_ports(bridge_t *bridge, com_port_t *port_a, com_port_t *port_b)
{
    if (bridge == NULL || port_a == NULL || port_b == NULL)
    {
        return;
    }
    bridge->port_a = port_a;
    bridge->port_b = port_b;
}

void bridge_set_idle_timeout(bridge_t *bridge, uint16_t idle_timeout)
{
    if (bridge == NULL)
    {
        return;
    }
    bridge->idle_timeout = idle_timeout;
}

void bridge_start(bridge_t *bridge)
{
    if (bridge == NULL)
    {
        return;
    }

    bridge->is_running = true;
    bridge->state = BRIDGE_STATE_INIT;
}

void bridge_stop(bridge_t *bridge)
{
    if (bridge == NULL)
    {
        return;
    }

    bridge->is_running = false;
}

bool bridge_is_running(bridge_t *bridge)
{
    if (bridge == NULL)
    {
        return false;
    }

    return bridge->is_running;
}

bool bridge_is_open(bridge_t *bridge)
{
    if (bridge == NULL)
    {
        return false;
    }

    return bridge->is_open;
}

static void bridge_tick(bridge_t *bridge)
{
    com_port_t *port_a;
    com_port_t *port_b;
    uint16_t    idle_timeout;
    uint32_t    idle_ticks;
    uint32_t    a_bytes;
    uint32_t    b_bytes;
    uint8_t     buff[BRIDGE_BUFFER_SZ];

    port_a = bridge->port_a;
    port_b = bridge->port_b;
    idle_timeout = bridge->idle_timeout;
    idle_ticks = bridge->_idle_ticks;

    switch(bridge->state)
    {
        case BRIDGE_STATE_INIT:
            bridge->state = BRIDGE_STATE_WAIT;
            ESP_LOGI(TAG, "bridge starting (%s <-> %s)", PORT_NAME(port_a), PORT_NAME(port_b));
        break;

        case BRIDGE_STATE_WAIT:
        {
            bool a_conn = port_a->is_connected(port_a);
            bool b_conn = port_b->is_connected(port_b);
            if (!a_conn || !b_conn)
            {
                if (!a_conn && !b_conn)
                    ESP_LOGW(TAG, "%s and %s disconnected", PORT_NAME(port_a), PORT_NAME(port_b));
                else
                    ESP_LOGW(TAG, "%s disconnected", !a_conn ? PORT_NAME(port_a) : PORT_NAME(port_b));

                if (!a_conn)
                    port_a->disconnect(port_a);
                if (!b_conn)
                    port_b->disconnect(port_b);

                port_a->flush(port_a);
                port_b->flush(port_b);
                bridge->is_open = false;
                bridge->_idle_ticks = 0;
                bridge->state = BRIDGE_STATE_CONNECT;
                break;
            }

            a_bytes = port_a->bytes_available(port_a);
            b_bytes = port_b->bytes_available(port_b);
            if (a_bytes > 0 || b_bytes > 0)
            {
                bridge->state = BRIDGE_STATE_SEND;
                break;
            }

            if (idle_ticks >= ((uint32_t)idle_timeout * 1000u / BRIDGE_TICK_MS))
            {
                ESP_LOGW(TAG, "idle timeout (%us), reconnecting", idle_timeout);
                bridge->_idle_ticks = 0;
                port_a->disconnect(port_a);
                port_b->disconnect(port_b);
                port_a->flush(port_a);
                port_b->flush(port_b);
                bridge->is_open = false;
                bridge->state = BRIDGE_STATE_CONNECT;
                break;
            }
            bridge->_idle_ticks++;
        }
        break;

        case BRIDGE_STATE_CONNECT:
        {
            bool a_ok = port_a->connect(port_a);
            bool b_ok = port_b->connect(port_b);
            if (!a_ok || !b_ok)
            {
                if (bridge->_idle_ticks == 0 || bridge->_idle_ticks % 500 == 0)
                    ESP_LOGW(TAG, "%s connect failed, retrying", !a_ok ? PORT_NAME(port_a) : PORT_NAME(port_b));
                bridge->_idle_ticks++;
                bridge->state = BRIDGE_STATE_CONNECT;
                break;
            }

            ESP_LOGI(TAG, "bridge open (%s <-> %s)", PORT_NAME(port_a), PORT_NAME(port_b));
            bridge->is_open = true;
            bridge->_idle_ticks = 0;
            bridge->state = BRIDGE_STATE_WAIT;
        break;
        }

        case BRIDGE_STATE_SEND:
            a_bytes = port_a->bytes_available(port_a);
            b_bytes = port_b->bytes_available(port_b);
            if (a_bytes > 0)
            {
                uint32_t len = (a_bytes > sizeof(buff)) ? sizeof(buff) : a_bytes;
                uint32_t rlen = port_a->read(port_a, buff, len);
                port_b->write(port_b, buff, rlen);
                ESP_LOGD(TAG, "%s -> %s: %u bytes", PORT_NAME(port_a), PORT_NAME(port_b), rlen);
            }

            if (b_bytes > 0)
            {
                uint32_t len = (b_bytes > sizeof(buff)) ? sizeof(buff) : b_bytes;
                uint32_t rlen = port_b->read(port_b, buff, len);
                port_a->write(port_a, buff, rlen);
                ESP_LOGD(TAG, "%s -> %s: %u bytes", PORT_NAME(port_b), PORT_NAME(port_a), rlen);
            }
            bridge->_idle_ticks = 0;
            bridge->state = BRIDGE_STATE_WAIT;
        break;
    }

}

static void bridge_task(void *pv)
{
    while (1)
    {
        for (uint16_t i = 0; i < _bridge_count; i++)
        {
            if (_bridges[i]->is_running)
            {
                bridge_tick(_bridges[i]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BRIDGE_TICK_MS));
    }
}

void bridge_manager_init(bridge_t **bridges, uint16_t count)
{
    if (bridges == NULL || count == 0)
    {
        return;
    }
    _bridges = bridges;
    _bridge_count = count;
}

void bridge_manager_start(void)
{
    if (_task_handle != NULL)
    {
        return;
    }

    xTaskCreate(bridge_task,
                "bridge_task",
                BRIDGE_TASK_STACK,
                NULL,
                BRIDGE_TASK_PRIO,
                &_task_handle);
}
