#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "http.h"
#include "i2s_interface.h"
#include "wifi.h"

#define TAG "Main"

extern "C" void app_main()
{
  // Initalize NVS
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(result);

  // Initalize WiFi and connect to configured network
  WiFi::init_station();

  // Initalize I2S Rx
  I2S::init();

  // Start the HTTP task
  xTaskCreate(HTTP::task, "HTTPTask", 8192, NULL, 4, NULL);

  while (true)
  {
    static I2S::sample_t samples[2 * I2S::BUFFER_SAMPLE_COUNT];
    size_t read = I2S::read(samples, sizeof(samples), portMAX_DELAY);
    
    // We should always read the full size
    assert(read == sizeof(samples));
    
    HTTP::queue_samples(samples);
  }
}
