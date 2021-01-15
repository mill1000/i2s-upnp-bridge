#include "esp_log.h"

#include <map>
#include <algorithm>

#include "json.h"
#include "nlohmann/json.hpp"
#include "nvs_interface.h"
#include "upnp_control.h"
#include "upnp_renderer.h"

#define TAG "JSON"

/**
  @brief  Build a JSON string of the known renderers
  
  @param  none
  @retval std::string
*/
std::string JSON::get_renderers()
{
  // Get the saved renderers from NVS
  std::map<std::string, std::string> nvs_renderers = NVS::get_renderers();

  // Create a map of UPNP::Renderer objects
  UpnpControl::renderer_map_t renderers;
  std::transform(nvs_renderers.begin(), nvs_renderers.end(), std::inserter(renderers, renderers.end()), [](const auto& kv) {
    return std::make_pair(kv.first, UPNP::Renderer(kv.first, kv.second));
  });

  // Populate renderer objects with discovered information
  UpnpControl::populate_renderer_info(renderers);
 
  nlohmann::json jRenderers = nlohmann::json::object();

  // Add an entry for each object
  for (auto& kv : renderers)
  {
    nlohmann::json& j = jRenderers[kv.first];
    const UPNP::Renderer& r = kv.second;

    j["uuid"] = r.uuid;
    j["name"] = r.name;
    j["control_url"] = r.control_url;
    j["selected"] = r.selected;
  }

  // Add renderer object to root
  nlohmann::json root;
  root["renderers"] = jRenderers;

  return root.dump();
}

