#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "ota_interface.h"

#define TAG "OTA"

static struct
{
  // This state is a very bad and lazy way of cordinating multiple OTA handles.
  OTA::State state;
} ota;

/**
  @brief  Helper function to cleanup OTA state at end or in case of error
  
  @param  none
  @retval esp_err_t
*/
esp_err_t OTA::AppHandle::cleanup()
{
  // Remove timeout timer
  if (this->timeout_timer != NULL)
  {
    if (xTimerDelete(this->timeout_timer, 0) == pdPASS)
      this->timeout_timer = NULL;
    else
      ESP_LOGW(TAG, "Failed to delete timeout timer.");
  }

  // Check for non-initalized or error state
  if (ota.state != State::InProgress)
  {
    ota.state = State::Idle;
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t result = esp_ota_end(this->handle);
  if (result != ESP_OK) 
  {
    ESP_LOGE(TAG, "esp_ota_end failed, err=0x%x.", result);
    ota.state = State::Idle;
    return result;
  }

  ota.state = State::Idle;

  return ESP_OK;
}

/**
  @brief  Initialize an OTA update. Checks partitions and begins OTA process.
  
  @param  none
  @retval esp_err_t
*/
esp_err_t OTA::AppHandle::start()
{
  // Don't attempt to re-init an ongoing OTA
  if (ota.state != State::Idle)
    return ESP_ERR_INVALID_STATE;

  // Check that the active and boot partition are the same otherwise we might be trying to double update
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* active = esp_ota_get_running_partition();
  if (boot != active)
    return ESP_ERR_INVALID_STATE;

  ESP_LOGI(TAG, "Boot partition type %d subtype %d at offset 0x%x.", boot->type, boot->subtype, boot->address);
  ESP_LOGI(TAG, "Active partition type %d subtype %d at offset 0x%x.", active->type, active->subtype, active->address);

  // Grab next update target
  const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
  if (target == NULL)
    return ESP_ERR_NOT_FOUND;

  ESP_LOGI(TAG, "Target partition type %d subtype %d at offset 0x%x.", target->type, target->subtype, target->address);

  esp_err_t result = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &this->handle);
  if (result != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ota_begin failed, error=0x%x.", result);
    return result;
  }

  // Create a timer that will handle timeout events
  this->timeout_timer = xTimerCreate("OTATimeout", pdMS_TO_TICKS(5000), false, (void*)this, [](TimerHandle_t t){
    ESP_LOGW(TAG, "Timeout during update. Performing cleanup...");
    OTA::Handle* handle = (OTA::Handle*) pvTimerGetTimerID(t);
    if (handle != nullptr)
      handle->cleanup();
  });

  // Start the timer
  if (xTimerStart(this->timeout_timer, pdMS_TO_TICKS(100)) != pdPASS)
    ESP_LOGE(TAG, "Failed to start timeout timer.");

  ota.state = State::InProgress;
  return ESP_OK;
}

/**
  @brief  Write firmware data during OTA
  
  @param  data Data buffer to write to handle
  @param  length Length of data buffer
  @retval esp_err_t
*/
esp_err_t OTA::AppHandle::write(uint8_t* data, uint16_t length)
{
  // Check for non-initialized or error state
  if (ota.state != State::InProgress)
    return ESP_ERR_INVALID_STATE;

  esp_err_t result = esp_ota_write(this->handle, data, length);
  if (result != ESP_OK) 
  {
    ESP_LOGE(TAG, "esp_ota_write failed, err=0x%x.", result);
    ota.state = State::Error;
    return result;
  }
  
  // Reset timeout timer
  if (this->timeout_timer != NULL)
    xTimerReset(this->timeout_timer, pdMS_TO_TICKS(10));

  return ESP_OK;
}

/**
  @brief  Finalize an OTA update. Sets boot partitions and returns callback to reboot
  
  @param  none
  @retval OTA::end_result_t
*/
OTA::end_result_t OTA::AppHandle::end()
{
  // Construct result object
  OTA::end_result_t result;
  result.callback = nullptr;

  // Perform clean up operations
  result.status = cleanup();
  if (result.status != ESP_OK) 
    return result;

  const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
  result.status = esp_ota_set_boot_partition(target);
  if (result.status != ESP_OK) 
  {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed, err=0x%x.", result.status);
    ota.state = State::Idle;
    return result;
  }

  const esp_partition_t* boot = esp_ota_get_boot_partition();
  ESP_LOGI(TAG, "Boot partition type %d subtype %d at offset 0x%x.", boot->type, boot->subtype, boot->address);

  // Success. Update status and set reboot callback
  result.status = ESP_OK;
  result.callback = []() {
    ota.state = State::Reboot;
    esp_restart();
  };
  
  return result;
}