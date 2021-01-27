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

    All = 0x00FFFFFF,
  };

  void task(void* pvParameters);
  void play();
  void stop();
  void update_selected_renderers();

  typedef std::map<std::string, UPNP::Renderer> renderer_map_t;

  renderer_map_t get_known_renderers();
}

#endif