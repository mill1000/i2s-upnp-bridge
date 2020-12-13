#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "i2s_interface.h"

#define TAG "Main"

extern "C" void app_main()
{
  // Initalize I2S Rx
  I2S::init();
}
