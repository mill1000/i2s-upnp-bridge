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

/**
  @brief  Parse the JSON settings received from the web interface 
          and store to the NVS
  
  @param  jString JSON string received from web interface
  @retval bool - JSON was valid and processed
*/
bool JSON::parse_renderers(const std::string& jString)
{
  nlohmann::json root = nlohmann::json::parse(jString, nullptr, false);
  if (root.is_discarded())
  {
    ESP_LOGE(TAG, "Invalid JSON received: %s", jString.c_str());
    return false;
  }

  // Parse renderers object
  if (root.contains("renderers"))
  {
    const nlohmann::json& jRenderers = root.at("renderers");
    std::map<std::string, std::string> selected;
    for (const auto& kv : jRenderers.items())
    {
      const nlohmann::json& jRenderer = kv.value();
      if (jRenderer["selected"].get<bool>())
        selected[jRenderer["uuid"].get<std::string>()] = jRenderer["name"].get<std::string>();
    }

    NVS::erase_renderers();
    NVS::set_renderers(selected);
  }
  
  // Trigger renderer update in UpnpControl
  UpnpControl::update_selected_renderers();

  return true;
}
