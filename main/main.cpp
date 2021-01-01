#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "main.h"
#include "http.h"
#include "i2s_interface.h"
#include "wifi.h"

#define TAG "Main"

/**
  @brief  Main system task which reads from I2S and updates system state
  
  @param  pvParameters
  @retval none
*/
void System::task(void* pvParameters)
{
  // Construct timer to periodically check state
  TimerHandle_t stateTimer = xTimerCreate("stateTimer", pdMS_TO_TICKS(250), pdFALSE, nullptr, [](TimerHandle_t xTimer ){});
  xTimerStart(stateTimer, portMAX_DELAY);

  int32_t timeout = 0;
  State state = State::Idle;

  while (true)
  {
    static I2S::sample_buffer_t samples;

    size_t read = I2S::read(samples.data(), sizeof(samples), portMAX_DELAY);
    assert(read == sizeof(samples)); // We should always read the full size

    // Queue samples to each client
    HTTP::queue_samples(samples);

    // Update system state when timer expires
    if (xTimerIsTimerActive(stateTimer) == pdTRUE)
      continue;
    
    // Restart timer
    xTimerStart(stateTimer, pdMS_TO_TICKS(10));

    // Test sample buffer and update timeouts
    if (samples.front() == 0 && samples.back() == 0)
    {
      if (timeout > 0)
        timeout--;
      else if (state == State::Active)
      {
        ESP_LOGI(TAG, "System idle.");
        state = State::Idle;
      }
    }
    else
    {
      if (timeout < ((state == State::Idle) ? IDLE_TIMEOUT: ACTIVE_TIMEOUT))
        timeout++;
      else if (state == State::Idle)
      {
        ESP_LOGI(TAG, "System active.");

        // Increase timeout in active state
        state = State::Active;
        timeout = ACTIVE_TIMEOUT;
      }
    }
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

  // Create a task which moves data from I2S to HTTP server
  xTaskCreate(System::task, "SystemTask", 4096, NULL, 2, NULL);
}
