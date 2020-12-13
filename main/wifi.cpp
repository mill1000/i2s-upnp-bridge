#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event_loop.h"

#include <cstring>
#include <string>

#include "wifi.h"

#define TAG "WiFi"

static uint32_t retry_count = WiFi::RETRY_COUNT;

/**
  @brief  Event handler for WiFi

  @param  arg Argument supplied when registering handler
  @param  event system_event_t of event
  @retval esp_err_t
*/
static esp_err_t wifiEventHandler(void* arg, system_event_t* event)
{
  switch (event->event_id)
  {
    case SYSTEM_EVENT_STA_START:
    {
      // // Set hostname before connection
      // std::string hostname = NVS::get_hostname();
      // if (!hostname.empty())
      // {
      //   ESP_LOGI(TAG, "Setting hostname to '%s'.", hostname.c_str());
      //   tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, hostname.c_str());
      // }

      esp_wifi_connect();
      break;
    }

    case SYSTEM_EVENT_STA_CONNECTED:
      ESP_LOGI(TAG, "Connected.");
      break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
    {
      if (retry_count--) 
      {
        ESP_LOGI(TAG, "Retrying connection... (%d/%d)", retry_count, WiFi::RETRY_COUNT);
        esp_wifi_connect();
      }
      else
        ESP_LOGE(TAG, "Failed to connect.");
      
      break;
    }

     case SYSTEM_EVENT_STA_GOT_IP:
    {
      system_event_sta_got_ip_t* got_ip = (system_event_sta_got_ip_t*) &event->event_info.got_ip;
      ESP_LOGI(TAG, "Assigned IP: %s", ip4addr_ntoa(&got_ip->ip_info.ip));
      
      // Reset retires
      retry_count = WiFi::RETRY_COUNT;

      break;
    }

    
    default:
      ESP_LOGW(TAG, "Unhandled event. ID: %d.", event->event_id);
      break;
  }

  return ESP_OK;
}

/**
  @brief  Initialize WiFi in station mode and connect to configured network.

  @param  none
  @retval none
*/
void WiFi::init_station()
{
  tcpip_adapter_init();

  ESP_ERROR_CHECK(esp_event_loop_init(&wifiEventHandler, NULL));

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));

  wifi_config_t wifi_config;
  memset(&wifi_config, 0, sizeof(wifi_config_t));

  // Set configured SSID and password
  strcpy((char*)wifi_config.sta.ssid,     CONFIG_WIFI_SSID);
  strcpy((char*)wifi_config.sta.password, CONFIG_WIFI_PASSWORD);

  // Require WPA2
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_LOGI(TAG, "Connecting to SSID '%s'...", wifi_config.sta.ssid);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}