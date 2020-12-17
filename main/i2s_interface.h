#ifndef __I2S_INTERFACE_H__
#define __I2S_INTERFACE_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace I2S
{
  constexpr int SAMPLE_FREQUENCTY = 48e3;

  // Total buffer space = 50 * 480 * 4 bytes -> 96 kB
  constexpr int BUFFER_SAMPLE_COUNT = 480;  // Number of samples to make a 10 ms chunk @ 48 kHz
  constexpr int BUFFER_COUNT = 50;  // 50 * 10 ms -> 500 ms of buffering

  typedef int16_t sample_t;

  void init(void);
  size_t read(sample_t samples[], size_t length = BUFFER_SAMPLE_COUNT * sizeof(sample_t), TickType_t waitTicks = portMAX_DELAY);
}

#endif