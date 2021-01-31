#ifndef __OTA_INTERFACE_H__
#define __OTA_INTERFACE_H__

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/timers.h"

#include <string>
#include <functional>

namespace OTA
{
  typedef std::function<void(void)> end_callback_t;
  
  struct end_result_t 
  {
    esp_err_t status;
    end_callback_t callback;
  };

  class Handle
  {
    public:
      virtual esp_err_t start(void) = 0;
      virtual esp_err_t write(uint8_t* data, uint16_t length) = 0;
      virtual end_result_t end(void) = 0;
      virtual esp_err_t cleanup(void) = 0;

      virtual ~Handle () {}
  };

  class AppHandle : public Handle
  {
    public:
      AppHandle() {}

      esp_err_t start(void);
      esp_err_t write(uint8_t* data, uint16_t length);
      end_result_t end(void);
      esp_err_t cleanup(void);

    private:
      TimerHandle_t timeoutTimer;
      esp_ota_handle_t handle;
  };

  enum class State
  {
    Idle,
    InProgress,
    Reboot,
    Error,
  };
};

#endif