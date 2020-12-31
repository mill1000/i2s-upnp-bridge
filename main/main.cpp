#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "http.h"
#include "i2s_interface.h"
#include "wifi.h"

#define TAG "Main"

/**
  @brief  Task function that reads and queues data from I2S
  
  @param  pvParameters
  @retval none
*/
static void i2s_read_task(void* pvParameters)
{
  while (true)
  {
    static I2S::sample_buffer_t samples;
    size_t read = I2S::read(samples.data(), sizeof(samples), portMAX_DELAY);

    // We should always read the full size
    assert(read == sizeof(samples));
    
    HTTP::queue_samples(samples);
  }
}

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

  // Create another task which will copy data from I2S to HTTP queues
  xTaskCreate(i2s_read_task, "I2STask", 4096, NULL, 2, NULL);
}
