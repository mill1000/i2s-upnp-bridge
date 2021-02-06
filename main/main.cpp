#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "http.h"
#include "nvs_interface.h"
#include "system.h"
#include "upnp_control.h"
#include "wifi.h"

#define TAG "Main"

/**
  @brief  Entry point for user application
  
  @param  none
  @retval none
*/
extern "C" void app_main()
{
  // Initialize NVS
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(result);

  // Initialize our own NVS interface
  NVS::init();

  // Initialize WiFi and connect to configured network
  WiFi::init_station();

  // Initialize I2S Rx
  I2S::init();

  // Start the HTTP task
  xTaskCreate(HTTP::task, "HTTPTask", 8192, NULL, 4, NULL);

  // Create a task which moves data from I2S to HTTP server
  xTaskCreate(System::task, "SystemTask", 4096, NULL, 2, NULL);

  // Create a task which handles sending UPNP events
  xTaskCreate(UpnpControl::task, "UpnpTask", 6144, NULL, 1, NULL);
}
