#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "system.h"
#include "http.h"
#include "i2s_interface.h"
#include "nvs_interface.h"
#include "upnp_control.h"

#define TAG "System"

static TaskHandle_t task_handle;

/**
  @brief  Main system task which reads from I2S and updates system state
  
  @param  pvParameters
  @retval none
*/
void System::task(void* pvParameters)
{
  task_handle = xTaskGetCurrentTaskHandle();

  // Construct timer to trigger state checks
  TimerHandle_t state_timer = xTimerCreate("stateTimer", pdMS_TO_TICKS(250), pdTRUE, task_handle, [](TimerHandle_t timer){
    xTaskNotify(pvTimerGetTimerID(timer), event_update_audio_state, eSetBits);
  });
  xTimerStart(state_timer, portMAX_DELAY);

  // Struct to track state of audio
  struct
  {
    int32_t timeout = 0;
    AudioState state = AudioState::Silent;
  } audio;

  // System state goes active when there are clients
  State state = State::Idle;

  while (true)
  {
    static I2S::sample_buffer_t samples;

    size_t read = I2S::read(samples.data(), sizeof(samples), portMAX_DELAY);
    assert(read == sizeof(samples)); // We should always read the full size

    // Queue samples to each client when active
    if (state == State::Active)
      HTTP::queue_samples(samples);
    else
      vTaskDelay(pdMS_TO_TICKS(250));
    
    // Check for events
    event_t events;
    if (xTaskNotifyWait(0, UINT32_MAX, (uint32_t*) &events, 0) != pdTRUE)
      continue;

    if (events & event_update_audio_state)
    {
      // Test audio samples for audio
      if (samples.front() == 0 && samples.back() == 0)
      {
        if (audio.timeout > 0)
          audio.timeout--;
        else if (audio.state != AudioState::Silent)
        {
          ESP_LOGI(TAG, "Audio off.");
          audio.state = AudioState::Silent;

          UpnpControl::disable();
        }
      }
      else
      {
        if (audio.timeout < ((audio.state == AudioState::Silent) ? AUDIO_SILENT_TIMEOUT: AUDIO_ACTIVE_TIMEOUT))
          audio.timeout++;
        else if (audio.state == AudioState::Silent)
        {
          ESP_LOGI(TAG, "Audio On.");

          // Increase timeout in active state
          audio.state = AudioState::Active;
          audio.timeout = AUDIO_ACTIVE_TIMEOUT;

          UpnpControl::enable();
        }
      }
    }

    if (events & event_set_active_state)
    {
      ESP_LOGI(TAG, "System active.");
      state = State::Active;

      // Flush the I2S interface since we've been sub-sampling in idle
      I2S::flush_rx();
    }

    if (events & event_set_idle_state)
    {
      ESP_LOGI(TAG, "System Idle.");
      state = State::Idle;
    }
  }
}

/**
  @brief  Set the system state to active
  
  @param  none
  @retval none
*/
void System::set_active_state()
{
  xTaskNotify(task_handle, event_set_active_state, eSetBits);
}

/**
  @brief  Set the system state to idle
  
  @param  none
  @retval none
*/
void System::set_idle_state()
{
  xTaskNotify(task_handle, event_set_idle_state, eSetBits);
}