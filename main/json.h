#ifndef __JSON_H__
#define __JSON_H__

#include <string>

#include "nlohmann/json.hpp"

namespace JSON
{
  template <typename T>
  T get_or_default(const nlohmann::json& json, const std::string& key, const T& default_value = T())
  {
    // If key is not null fetch value otherwise use default. Because dumb: https://github.com/nlohmann/json/issues/1163
    return json[key].is_null() ? default_value : json[key].get<T>();
  }

  template <typename T>
  T get_or_default(const nlohmann::json& json, uint32_t index, const T& default_value = T())
  {
    return json[index].is_null() ? default_value : json[index].get<T>();
  }
  
  template <typename T, class UnaryPredicate> 
  void set_if_valid(nlohmann::json& json, const std::string& key, const T& value, UnaryPredicate validator)
  {
    json[key] = validator(value) ? value : nullptr;
  }

  std::string get_renderers();
}

#endif