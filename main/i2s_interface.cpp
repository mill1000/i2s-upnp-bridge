#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2s_interface.h"

#define TAG "I2S"

void I2S::init()
{
  i2s_pin_config_t pin_config;
  memset(&pin_config, 0, sizeof(i2s_pin_config_t));

  // Enable inputs for RX mode
  pin_config.bck_io_num = GPIO_NUM_14;
  pin_config.ws_io_num = GPIO_NUM_12;
  pin_config.data_in_num = GPIO_NUM_27;

  // Enable pins
  i2s_set_pin(I2S_NUM_0, &pin_config);

  i2s_config_t config;
  memset(&config, 0, sizeof(i2s_config_t));

  config.mode = (i2s_mode_t) (I2S_MODE_SLAVE | I2S_MODE_RX);
  config.sample_rate = I2S::SAMPLE_FREQUENCTY;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  config.dma_buf_count = I2S::BUFFER_COUNT;
  config.dma_buf_len = I2S::BUFFER_SAMPLE_COUNT;
  config.use_apll = true; // Use the better lock
  config.fixed_mclk = 0; // Use automatic dividers
  config.intr_alloc_flags = 0; // ??

  // Configre the I2S driver
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
}

size_t I2S::read(sample_t samples[], size_t length, TickType_t waitTicks)
{
  size_t read = 0;
  i2s_read(I2S_NUM_0, samples, length, &read, waitTicks);

  return read;
}