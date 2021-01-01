#ifndef __MAIN_H__
#define __MAIN_H__

namespace System
{
  // Timeouts in 250 ms ticks
  constexpr int IDLE_TIMEOUT = 20; // 5 seconds
  constexpr int ACTIVE_TIMEOUT = 60; // 15 seconds

  enum class State
  {
    Idle,
    Active,
  };

  void task(void* pvParameters);
}

#endif