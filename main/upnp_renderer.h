#ifndef __UPNP_RENDERER_H__
#define __UPNP_RENDERER_H__

#include <string>

namespace UPNP
{
  class Renderer
  {
    public:
      const std::string uuid;
      std::string name;
      std::string control_url;
      bool selected = false;

      Renderer(const std::string& uuid) : uuid(uuid) {}
      Renderer(const std::string& uuid, const std::string& name) : uuid(uuid), name(name) {}
  };
}

#endif
