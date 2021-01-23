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

      Renderer() {}
      Renderer(const std::string& uuid, const std::string& name = "", const std::string& url = "") : uuid(uuid), name(name), control_url(url) {}

      bool valid(void)
      {
        if (control_url.empty())
          return false;

        if (control_url.empty())
          return false;

        if (control_url.empty())
          return false;

        return true;
      }
  };
}

#endif
