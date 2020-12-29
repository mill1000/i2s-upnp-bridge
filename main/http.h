#ifndef __HTTP_H__
#define __HTTP_H__

namespace HTTP
{
  // Maximum length of outgoing buffer
  constexpr int MAX_SEND_BUFFER_LENGTH = 10 * 1024;

  void task(void* pvParameters);
}

#endif