#ifndef __UPNP_CONTROL_H__
#define __UPNP_CONTROL_H__

#include <string>

namespace UpnpControl
{
  enum class Event
  {
    // Events to start & stop playback on the renderer
    Play = 1 << 0,
    Stop = 1 << 1,

    // Events to trigger specific UPNP actions
    SendSetUriAction = 1 << 2,
    SendPlayAction = 1 << 3,
    SendStopAction = 1 << 4,
    All = 0x00FFFFFF,
  };

  typedef void (*event_callback_t)(int code, const std::string& response);

  void task(void* pvParameters);
  void play();
  void stop();
}

#endif