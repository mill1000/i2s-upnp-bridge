#ifndef __HTTP_H__
#define __HTTP_H__

#include "i2s_interface.h"

namespace HTTP
{
  // Maximum length of outgoing buffer
  constexpr int CLIENT_MAX_SEND_BUFFER_LENGTH = 10 * 1024;
  constexpr int CLIENT_QUEUE_LENGTH = 10;

  void task(void* pvParameters);

  void queue_samples(const I2S::sample_buffer_t& samples);
}

#endif