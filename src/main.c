/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   main.c
 */

#include "bridge.h"
#include "com_port.h"
#include "uart32.h"
#include "wifi32.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define UART2_TX_PIN   (17u)
#define UART2_RX_PIN   (16u)
#define UART_BUFFER_SZ (16384u)
#define UART_NUM       (2)

#define COM_PORT_MAX (2u)

#define WIFI_SSID      "203@Sofia_apartments"
#define WIFI_PASSWORD  "EDfFbwqJWVkF"
#define WIFI_RETRY_MAX (3u)

com_port_t _port[COM_PORT_MAX];
bridge_t   _bridge;

com_port_t *port[COM_PORT_MAX];
bridge_t   *bridge;

static const char        *TAG      = "main";
static uint8_t            retries  = 0;
static const uart32_cfg_t uart_cfg = { 115200,
                                       UART_DATA_8_BITS,
                                       UART_PARITY_DISABLE,
                                       UART_STOP_BITS_1,
                                       UART_HW_FLOWCTRL_DISABLE,
                                       UART2_TX_PIN,
                                       UART2_RX_PIN,
                                       UNUSED_PIN,
                                       UNUSED_PIN,
                                       UART_BUFFER_SZ };

typedef struct
{
   uint16_t        num;
   com_port_type_t type;
} port_cfg_t;

typedef struct
{
   port_cfg_t ports[COM_PORT_MAX];
} app_cfg_t;

app_cfg_t app_cfg = { .ports = {
                          { .num = 3000, .type = COM_PORT_TYPE_TCP },
                          { .num = UART_NUM, .type = COM_PORT_TYPE_UART },
                      } };

void app_main (void)
{
   esp_err_t err;
   uint32_t  c         = 0;
   uint8_t   buff[255] = { 0 };

   err = nvs_flash_init ();
   // handle specific NVS initialization errors
   if (err == ESP_ERR_NVS_NO_FREE_PAGES
       || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
   {
      // if the NVS partition is corrupted or needs to be erased, erase it
      // and retry initialization
      ESP_ERROR_CHECK (nvs_flash_erase ());
      err = nvs_flash_init (); // retry initialization after erasing
   }

   if (UARTInit (UART_NUM, &uart_cfg) != UART_OK)
   {
      ESP_LOGE (TAG, "UART%d init FAIL", UART_NUM);
   }

   if (WIFIInit () != WIFI_OK)
   {
      ESP_LOGE (TAG, "WIFI init FAIL");
   }

   if (WIFIConnect (WIFI_SSID, WIFI_PASSWORD) != WIFI_OK)
   {
      ESP_LOGE (TAG, "WifiConnect ERROR");
   }

   while (WIFIIsConnected () != true) // wait for wifi to connect
   {
      if (retries >= WIFI_RETRY_MAX)
      {
         ESP_LOGE (TAG, "WiFi connection failed after %d retries",
                   WIFI_RETRY_MAX);
         return;
      }
      retries++;
      ESP_LOGI (TAG, "Waiting for WiFi connection (attempt %d/%d)...", retries,
                WIFI_RETRY_MAX);

      vTaskDelay (pdMS_TO_TICKS (2000));
   }

   uint32_t ip_addr = WIFIGetIP ();
   ESP_LOGI (TAG, "IP Address " IPSTR, IP2STR ((esp_ip4_addr_t *)&ip_addr));

   for (int i = 0; i < COM_PORT_MAX; i++)
   {
      port[i] = &_port[i];

      com_port_init (port[i], app_cfg.ports[i].type);
      com_port_set_num (port[i], app_cfg.ports[i].num);
      ESP_LOGI (TAG, "port %d(%s) init OK", i + 1,
                port[i]->type == COM_PORT_TYPE_UART ? "UART" : "TCP");
   }

   bridge = &_bridge;
   bridge_init (bridge, port[0], port[1]);
   bridge_start (bridge);
   ESP_LOGI (TAG, "bridge started");

   bridge_manager_init (&bridge, 1);
   bridge_manager_start ();
   esp_log_level_set ("bridge", ESP_LOG_DEBUG);

   vTaskPrioritySet (NULL, 5);
   while (1)
   {
      ESP_LOGI (TAG, "Hello %d", c);
      c++;

      vTaskDelay (pdMS_TO_TICKS (1000));
   }
}