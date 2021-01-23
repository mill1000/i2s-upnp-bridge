#ifndef __SSDP_H__
#define __SSDP_H__

#include <string>

#include "upnp_renderer.h"

namespace SSDP
{
  UPNP::Renderer parse_description(const std::string& host, const std::string& desc);
}

#endif
