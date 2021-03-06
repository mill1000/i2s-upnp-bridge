#ifndef __UPNP_CONTROL_H__
#define __UPNP_CONTROL_H__

#include <string>
#include <map>

#include "upnp_renderer.h"

namespace UpnpControl
{
  constexpr int EVENT_QUEUE_LENGTH = 5;

  enum class Event
  {
    Enable,
    Disable,
    UpdateSelectedRenderers,

    // "Private" events
    SendPlayAction,
    SendStopAction,
  };

  void task(void* pvParameters);
  void enable();
  void disable();
  void update_selected_renderers();

  typedef std::map<std::string, UPNP::Renderer> renderer_map_t;

  renderer_map_t get_known_renderers();
}

#endif