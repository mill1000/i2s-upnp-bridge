#ifndef __HTTP_H__
#define __HTTP_H__

#include "mongoose.h"
#include "i2s_interface.h"

namespace HTTP
{
  constexpr int CLIENT_QUEUE_LENGTH = 3;

  // Object to represent a stream configuration
  struct StreamConfig
  {
    const char* const name;
    const char* const headers;
    void (*setup)(struct mg_connection* nc) = nullptr;

    StreamConfig(const char* name, const char* headers) : name(name), headers(headers) {}
  };

  void task(void* pvParameters);
  void queue_samples(const I2S::sample_buffer_t& samples);
}

#endif