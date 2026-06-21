/*
 * Author: Dayo
 * Date:   06/20/2026
 * File:   wifi32.c
 */

#include "wifi32.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

#include <string.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

static const char *TAG = "wifi32";

static uint32_t s_ip_addr;

static volatile bool sta_connected = false;
static volatile bool ip_obtained   = false;

esp_event_handler_instance_t instance_any_id;
esp_event_handler_instance_t instance_got_ip;

static void wifi_event_handler (void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);
static void ip_event_handler (void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);

wifi_err_t WIFIInit (void)
{
   esp_err_t err = ESP_OK;

   err = esp_netif_init ();
   if (err != ESP_OK)
   {
      return (WIFI_ERR_INIT);
   }

   err = esp_event_loop_create_default ();
   if (err != ESP_OK)
   {
      return (WIFI_ERR_INIT);
   }

   esp_netif_t *p_netif = esp_netif_create_default_wifi_sta ();
   if (!p_netif)
   {
      return (WIFI_ERR_INIT);
   }

   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT ();
   err                    = esp_wifi_init (&cfg);
   if (err != ESP_OK)
   {
      return (WIFI_ERR_INIT);
   }

   err = esp_event_handler_instance_register (WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifi_event_handler, NULL,
                                              &instance_any_id);
   if (err != ESP_OK)
   {
      return (WIFI_ERR_INIT);
   }

   err = esp_event_handler_instance_register (IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &ip_event_handler, NULL,
                                              &instance_got_ip);
   if (err != ESP_OK)
   {
      return (WIFI_ERR_INIT);
   }

   return (WIFI_OK);
}

wifi_err_t WIFIConnect (const char *ssid, const char *password)
{
   esp_err_t     err         = ESP_OK;
   wifi_config_t wifi_config = { 0 };

   if (ssid == NULL || password == NULL)
   {
      return WIFI_ERR_INVALID_CREDENTIALS;
   }

   if (ssid[0] == '\0' || password[0] == '\0')
   {
      return WIFI_ERR_INVALID_CREDENTIALS;
   }

   err = esp_wifi_set_mode (WIFI_MODE_STA);
   if (err != ESP_OK)
   {
      return WIFI_ERR_CONNECT;
   }

   err = esp_wifi_start ();
   if (err != ESP_OK)
   {
      return WIFI_ERR_CONNECT;
   }

   /* Disable WiFi power saving to prevent disconnections */
   err = esp_wifi_set_ps (WIFI_PS_NONE);
   if (err != ESP_OK)
   {
      ESP_LOGW (TAG, "Failed to disable WiFi power saving: %s",
                esp_err_to_name (err));
   }
   else
   {
      ESP_LOGI (TAG, "WiFi power saving disabled");
   }

   const size_t ssid_len = min (strlen (ssid), sizeof (wifi_config.sta.ssid));
   const size_t password_len
       = min (strlen (password), sizeof (wifi_config.sta.password));

   memcpy (wifi_config.sta.ssid, ssid, ssid_len);
   memcpy (wifi_config.sta.password, password, password_len);

   err = esp_wifi_set_config (WIFI_IF_STA, &wifi_config);
   if (err != ESP_OK)
   {
      return (WIFI_ERR_CONNECT);
   }

   /* Configure WiFi to be more stable */
   esp_wifi_set_bandwidth (WIFI_IF_STA, WIFI_BW_HT20);
   esp_wifi_set_max_tx_power (78); /* 19.5 dBm, near maximum */

   /* Disable 802.11n to avoid compatibility issues */
   esp_wifi_set_protocol (WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);

   err = esp_wifi_connect ();
   if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
   {
      return (WIFI_ERR_CONNECT);
   }

   return WIFI_OK;
}

wifi_err_t WIFIDisconnect (void)
{
   esp_err_t err = ESP_OK;

   err = esp_wifi_disconnect ();
   if (err != ESP_OK)
   {
      return (WIFI_ERR_DISCONNECT);
   }

   return (WIFI_OK);
}

bool WIFIIsConnected (void) { return sta_connected && ip_obtained; }

static void wifi_event_handler (void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
   if (WIFI_EVENT == event_base)
   {
      const wifi_event_t event_type = (wifi_event_t)event_id;
      switch (event_type)
      {
         case WIFI_EVENT_STA_DISCONNECTED:
         {
            sta_connected = false;
            ip_obtained   = false;
            wifi_event_sta_disconnected_t *disconn
                = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW (TAG, "WiFi disconnected (reason: %d)", disconn->reason);
            esp_wifi_connect ();
            break;
         }

         case WIFI_EVENT_STA_CONNECTED:
            sta_connected = true;
            ESP_LOGI (TAG, "WiFi connected");
            break;

         case WIFI_EVENT_STA_START:
            ESP_LOGI (TAG, "WiFi started");
            break;

         case WIFI_EVENT_STA_STOP:
            ESP_LOGI (TAG, "WiFi stopped");
            sta_connected = false;
            ip_obtained   = false;
            break;

         default:
            ESP_LOGD (TAG, "WiFi event: %d", event_type);
            break;
      }
   }
}

static void ip_event_handler (void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
   switch ((ip_event_t)event_id)
   {
      case IP_EVENT_STA_GOT_IP:
      {
         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
         s_ip_addr                = event->ip_info.ip.addr;
         ip_obtained              = true;
         ESP_LOGI (TAG, "Got IP address: " IPSTR, IP2STR (&event->ip_info.ip));
      }
      break;

      case IP_EVENT_STA_LOST_IP:
         ip_obtained = false;
         s_ip_addr   = 0;
         ESP_LOGW (TAG, "Lost IP address");
         break;

      default:
         ESP_LOGD (TAG, "IP event: %d", event_id);
         break;
   }
}

uint32_t WIFIGetIP (void) { return s_ip_addr; }

int8_t WIFIGetRSSI (void)
{
   wifi_ap_record_t ap_info;

   if (esp_wifi_sta_get_ap_info (&ap_info) != ESP_OK)
   {
      return 0;
   }

   return ((int8_t)ap_info.rssi);
}
