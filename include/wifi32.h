/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   wifi32.h
 */

#ifndef WIFI32_H
#define WIFI32_H

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#include "lwip/err.h"
#include "lwip/sys.h"

typedef enum
{
   WIFI_OK = 0,
   WIFI_ERR_INIT,
   WIFI_ERR_CONNECT,
   WIFI_ERR_DISCONNECT,
   WIFI_ERR_INVALID_CREDENTIALS,
   WIFI_ERR_NOT_INITIALIZED,
} wifi_err_t;

wifi_err_t WIFIInit (void);
wifi_err_t WIFIConnect (const char *ssid, const char *password);
wifi_err_t WIFIDisconnect (void);
bool       WIFIIsConnected (void);
uint32_t   WIFIGetIP (void);   // returns IP as uint32_t
int8_t     WIFIGetRSSI (void); // signal strength, useful for debugging

#endif // WIFI32_H
