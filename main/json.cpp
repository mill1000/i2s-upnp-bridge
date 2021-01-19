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
  // Get all renders known to UPNP
  UpnpControl::renderer_map_t renderers = UpnpControl::get_known_renderers();
 
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

