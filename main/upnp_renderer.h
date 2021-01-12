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

      Renderer(const std::string& uuid) : uuid(uuid) {}
  };
}

#endif
