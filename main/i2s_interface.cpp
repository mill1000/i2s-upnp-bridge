#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2s_interface.h"

#define TAG "I2S"

void I2S::init()
{
  i2s_config_t config;
  memset(&config, 0, sizeof(i2s_config_t));

  config.mode = (i2s_mode_t) (I2S_MODE_SLAVE | I2S_MODE_RX);
  config.sample_rate = 48e3;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = (i2s_comm_format_t) (I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;

  // Configre the I2S driver
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);

  i2s_pin_config_t pin_config;
  memset(&pin_config, 0, sizeof(i2s_pin_config_t));

  // Enable inputs for RX mode
  pin_config.bck_i_en = true;
  pin_config.ws_i_en = true;
  pin_config.data_in_en = true;

  // Enable pins
  i2s_set_pin(I2S_NUM_0, &pin_config);

  int16_t samples[4];
  while(1)
  {
    size_t read = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, samples, 4, &read, portMAX_DELAY);
    if (result != ESP_OK)
    {
      ESP_LOGW(TAG, "Failed to read I2S samples.");
      continue;
    }

    ESP_LOGI(TAG, "Read samples %d %d", samples[0], samples[1]);
  }
}