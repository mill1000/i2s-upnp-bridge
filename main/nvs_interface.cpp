#include "nvs_flash.h"
#include "esp_log.h"

#include <string>
#include <vector>

#include "nvs_interface.h"
#include "nvs_parameters.h"

#define TAG "NVS"

static NvsHelper nvs_renderers(NVS::RENDERERS_NAMESPACE);

/**
  @brief  Callback function for the NVS helper to report errors
  
  @param  name Namespace that caused the error
  @param  key The paramter key that caused the error
  @param  result The esp_error_t of the error
  @retval none
*/
static void helper_callback(const std::string& name, const std::string& key, esp_err_t result)
{
  ESP_LOGW(TAG, "NVS Error. Namespace '%s' Key '%s' Error: %s", name.c_str(), key.c_str(), esp_err_to_name(result));
}

/**
  @brief  Open the NVS namespace and initialize our parameter object
  
  @param  none
  @retval none
*/
void NVS::init()
{
  ESP_LOGI(TAG, "Initializing NVS interface.");

  // Open a namespace to hold the renderers
  if (nvs_renderers.open(&helper_callback) != ESP_OK)
  {
    ESP_LOGE(TAG, "Error opening NVS namespace '%s'.", RENDERERS_NAMESPACE);
    return;
  }

  uint8_t version = UINT8_MAX;
  if (nvs_renderers.nvs_get<uint8_t>("version", version) == ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Invalid NVS version in namespace '%s'. Erasing.", RENDERERS_NAMESPACE);
    erase_renderers();
  }
}

/**
  @brief  Erase all data in the schedule NVS. Does not commit!
  
  @param  none
  @retval none
*/
void NVS::erase_renderers()
{
  nvs_renderers.erase_all();

  // Restore the version byte
  nvs_renderers.nvs_set<uint8_t>("version", NVS_VERSION);
  nvs_renderers.commit();
}

/**
  @brief  Save renderers in NVS
  
  @param  renderer_map std::map<std::string, std::string> of renderer UUID and name
  @retval none
*/
void NVS::set_renderers(const std::map<std::string, std::string>& renderer_map)
{
  size_t index = 0;
  for (const auto& kv : renderer_map)
  {
    // NVS system won't allow large keys so we can't key by UUID
    // instead we will key both items by an index
    char key[16] = {0};
    snprintf(key, 16, "name%d", index);
    nvs_renderers.nvs_set<std::string>(key, kv.first);

    snprintf(key, 16, "uuid%d", index);
    nvs_renderers.nvs_set<std::string>(key, kv.second);

    index++;
  }

  nvs_renderers.commit();
}

/**
  @brief  Fetch renderers from NVS
  
  @param  none
  @retval std::map<std::string, std::string>
*/
std::map<std::string, std::string> NVS::get_renderers()
{
  // Find all name keys in the renderers NVS
  const std::vector<std::string> names = nvs_renderers.nvs_find(NVS_TYPE_STR, "name");

  std::map<std::string, std::string> renderer_map;
  for (size_t i = 0; i < names.size(); i++)
  {
    char key[16] = {0};
    snprintf(key, 16, "name%d", i);

    std::string name;
    if (nvs_renderers.nvs_get<std::string>(key, name) != ESP_OK)
      ESP_LOGW(TAG, "Failed to get NVS renderer entry for '%s'", key);

    snprintf(key, 16, "uuid%d", i);
    if (nvs_renderers.nvs_get<std::string>(key, renderer_map[name]) != ESP_OK)
      ESP_LOGW(TAG, "Failed to get NVS renderer entry for '%s'", key);
  }

  return renderer_map;
}