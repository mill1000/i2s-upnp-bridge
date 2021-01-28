#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "main.h"
#include "http.h"
#include "i2s_interface.h"
#include "nvs_interface.h"
#include "upnp_control.h"
#include "wifi.h"

#define TAG "Main"

/**
  @brief  Main system task which reads from I2S and updates system state
  
  @param  pvParameters
  @retval none
*/
void System::task(void* pvParameters)
{
  // Construct timer to trigger state checks
  TimerHandle_t stateTimer = xTimerCreate("stateTimer", pdMS_TO_TICKS(250), pdTRUE, xTaskGetCurrentTaskHandle(), [](TimerHandle_t timer){
    xTaskNotifyGive(pvTimerGetTimerID(timer));
  });
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

    // Update system state when notified
    if (ulTaskNotifyTake(pdTRUE, 0) == 0)
      continue;

    // Test sample buffer and update timeouts
    if (samples.front() == 0 && samples.back() == 0)
    {
      if (timeout > 0)
        timeout--;
      else if (state == State::Active)
      {
        ESP_LOGI(TAG, "System idle.");
        state = State::Idle;

        UpnpControl::disable();
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

        UpnpControl::enable();
      }
    }
  }
}

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
