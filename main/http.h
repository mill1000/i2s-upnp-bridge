#ifndef __HTTP_H__
#define __HTTP_H__

#include "i2s_interface.h"

namespace HTTP
{
  // Maximum length of outgoing buffer
  constexpr int MAX_SEND_BUFFER_LENGTH = 10 * 1024;

  void task(void* pvParameters);

  void queue_samples(const I2S::sample_t* samples);
}

#endif