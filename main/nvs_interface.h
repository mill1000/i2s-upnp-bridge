#ifndef __NVS_INTERFACE_H__
#define __NVS_INTERFACE_H__

#include <vector>
#include <string>
#include <map>

namespace NVS
{
  constexpr const char* RENDERERS_NAMESPACE = "renderers";

  constexpr uint8_t NVS_VERSION = 0;

  void init(void);

  void erase_renderers(void);
  
  void set_renderers(const std::map<std::string, std::string>& renderers);
  std::map<std::string, std::string> get_renderers(void);
}

#endif