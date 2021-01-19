#ifndef __UPNP_CONTROL_H__
#define __UPNP_CONTROL_H__

#include <string>
#include <map>

#include "upnp_renderer.h"

namespace UpnpControl
{
  enum class Event
  {
    // Events to control the task loop
    Play = 1 << 0,
    Stop = 1 << 1,
    UpdateSelectedRenderers = 1 << 2,

    // Events to trigger specific UPNP actions
    SendSetUriAction = 1 << 8,
    SendPlayAction = 1 << 9,
    SendStopAction = 1 << 10,
    All = 0x00FFFFFF,
  };

  typedef void (*event_callback_t)(int code, const std::string& response);

  void task(void* pvParameters);
  void play();
  void stop();
  void update_selected_renderers();

  typedef std::map<std::string, UPNP::Renderer> renderer_map_t;

  renderer_map_t get_known_renderers();
}

#endif