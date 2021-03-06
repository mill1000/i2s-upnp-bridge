#ifndef __I2S_INTERFACE_H__
#define __I2S_INTERFACE_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>

namespace I2S
{
  constexpr int SAMPLE_FREQUENCTY = 48e3;

  // Total buffer space = 3 * 480 * 4 bytes -> 5.76 kB
  constexpr int BUFFER_SAMPLE_COUNT = 480;  // Number of samples to make a 10 ms chunk @ 48 kHz
  constexpr int BUFFER_COUNT = 3;  // 3 * 10 ms -> 30 ms of buffering

  typedef int16_t sample_t;
  typedef std::array<sample_t, 2 * BUFFER_SAMPLE_COUNT> sample_buffer_t;

  void init(void);
  size_t read(sample_t* samples, size_t length = sizeof(sample_buffer_t), TickType_t wait_ticks = portMAX_DELAY);
  void flush_rx(void);
  void reset(void);
}

#endif