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

/**
  @brief  Initialize the I2S interface

  @param  none
  @retval none
*/
void I2S::init()
{
  i2s_config_t config;
  memset(&config, 0, sizeof(i2s_config_t));

  config.mode = (i2s_mode_t) (I2S_MODE_SLAVE | I2S_MODE_RX);
  config.sample_rate = I2S::SAMPLE_FREQUENCTY;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 2, 0)
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
  config.communication_format = (i2s_comm_format_t) (I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
  config.dma_buf_count = I2S::BUFFER_COUNT;
  config.dma_buf_len = I2S::BUFFER_SAMPLE_COUNT;
  config.use_apll = true; // Use the better lock
  config.fixed_mclk = 0; // Use automatic dividers
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; // ??

  // Configure the I2S driver
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);

  i2s_pin_config_t pin_config;
  memset(&pin_config, 0, sizeof(i2s_pin_config_t));

  // Enable inputs for RX mode
  pin_config.bck_io_num = GPIO_NUM_14;
  pin_config.ws_io_num = GPIO_NUM_26;
  pin_config.data_in_num = GPIO_NUM_27;

  // Enable pins
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

/**
  @brief  Attempt to read from the I2S interface.

  @param  samples Buffer to store sample data to
  @param  length Number of bytes (not samples!) to read
  @param  wait_ticks Number of ticks to wait for data.
  @retval size_t - Actual number of bytes read
*/
size_t I2S::read(sample_t samples[], size_t length, TickType_t wait_ticks)
{
  size_t read = 0;
  i2s_read(I2S_NUM_0, samples, length, &read, wait_ticks);
  
  return read;
}

/**
  @brief  Flush the RX queues by reading all available data

  @param  none
  @retval none
*/
void I2S::flush_rx()
{
  // Read until there's nothing left
  size_t read = 0;
  do
  {
    I2S::sample_buffer_t dummy;
    i2s_read(I2S_NUM_0, dummy.data(), sizeof(dummy), &read, 0);
  } while (read);
}

/**
  @brief  Attempt to reset the I2S peripheral

  @param  none
  @retval none
*/
void I2S::reset()
{
  // Toggle the driver
  i2s_stop(I2S_NUM_0);
  i2s_start(I2S_NUM_0);

  // Flush the RX data
  flush_rx();
}