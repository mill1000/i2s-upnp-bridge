#ifndef __SYSTEM_H__
#define __SYSTEM_H__

namespace System
{
  // Timeouts in 250 ms ticks
  constexpr int AUDIO_SILENT_TIMEOUT = 20; // 5 seconds
  constexpr int AUDIO_ACTIVE_TIMEOUT = 60; // 15 seconds

  enum class State
  {
    Idle,
    Active,
  };

  enum class AudioState
  {
    Silent,
    Active,
  };

  typedef enum event_t
  {
    event_update_audio_state = 1 << 0,
    event_set_idle_state     = 1 << 1,
    event_set_active_state   = 1 << 2,
  } event_t;

  void set_active_state(void);
  void set_idle_state(void);

  void task(void* pvParameters);
}

#endif