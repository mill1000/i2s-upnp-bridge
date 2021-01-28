#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "lwip/igmp.h"

#include <string>
#include <map>

#include "ssdp.h"
#include "upnp_control.h"
#include "upnp.h"
#include "upnp_renderer.h"
#include "mongoose.h"
#include "nvs_interface.h"
#include "tinyxml2.h"
#include "utils.h"

#define TAG "UPNP"

static QueueHandle_t upnpEventQueue;
static UpnpControl::renderer_map_t discoveredRenderers;
static SemaphoreHandle_t rendererMutex;

/**
  @brief  Parse the SSDP description XML into a UPNP::Renderer
  
  @param  host The hostname or IP of the device
  @param  desc Description received from the device
  @retval UPNP::Renderer
*/
UPNP::Renderer SSDP::parse_description(const std::string& host, const std::string& desc)
{
  // Build the XML document
  tinyxml2::XMLDocument xml_document;
  xml_document.Parse(desc.c_str());

  tinyxml2::XMLElement* root = xml_document.FirstChildElement("root");
  if (root == nullptr)
  {
    ESP_LOGE(TAG, "Invalid description XML. No root element.");
    return UPNP::Renderer();
  }

  // Start decoding the device element for useful information
  tinyxml2::XMLElement* device = root->FirstChildElement("device");
  if (device == nullptr)
  {
    ESP_LOGE(TAG, "Invalid description XML. Could not locate device element.");
    return UPNP::Renderer();
  }

  // Extract friendly name from device description
  tinyxml2::XMLElement* friendlyName = device->FirstChildElement("friendlyName");
  if (friendlyName == nullptr)
  {
    ESP_LOGE(TAG, "Invalid description XML. Could not locate friendlyName element.");
    return UPNP::Renderer();
  }

  std::string name = std::string(friendlyName->GetText());

  // Extract UDN from device description
  tinyxml2::XMLElement* UDN = device->FirstChildElement("UDN");
  if (UDN == nullptr)
  {
    ESP_LOGE(TAG, "Invalid description XML. Could not locate UDN element.");
    return UPNP::Renderer();
  }

  // Sscanf the UUID since std::regex is stack hungry
  char uuid_buffer[256] = {0};
  if (sscanf(UDN->GetText(), "uuid:%255s", uuid_buffer) != 1)
  {
    ESP_LOGE(TAG, "Could not extract UUID from UDN: %s", UDN->GetText());
    return UPNP::Renderer();
  }

  std::string uuid(uuid_buffer);
  if (uuid.empty())
  {
    ESP_LOGE(TAG, "Invalid UUID for renderer: %s.", uuid.c_str());
    return UPNP::Renderer();
  }

  // TODO icons

  // Grab service list to search for AVTransport
  tinyxml2::XMLElement* serviceList = device->FirstChildElement("serviceList");
  if (serviceList == nullptr)
  {
    ESP_LOGE(TAG, "Invalid description XML. Could not locate serviceList element.");
    return UPNP::Renderer();
  }

  // Extract the control URL from the AVTransport service
  std::string control_url;
  tinyxml2::XMLElement* service = serviceList->FirstChildElement();
  while (service != nullptr) // Scan all services in serviceList
  {
    tinyxml2::XMLElement* serviceType = service->FirstChildElement("serviceType");
    if (serviceType == nullptr || std::string(serviceType->GetText()).compare("urn:schemas-upnp-org:service:AVTransport:1") != 0)
    {
      // Can't find serviceType element, or not AVTransport service
      service = service->NextSiblingElement();
      continue;
    }

    // Grab control URL from matching service
    tinyxml2::XMLElement* controlURL = service->FirstChildElement("controlURL");
    if (controlURL != nullptr)
    {
      control_url = std::string(controlURL->GetText());
      break;
    }
    
    // Fetch next service
    service = service->NextSiblingElement();
    continue;
  }

  if (control_url.empty())
  {
    ESP_LOGE(TAG, "Could not find control URL for AVTransport service.");
    return UPNP::Renderer();
  }
  
  // Grab the base URL if it exists
  std::string base_url;
  tinyxml2::XMLElement* urlBase = root->FirstChildElement("URLBase");
  if (urlBase != nullptr)
    base_url = std::string(urlBase->GetText());

  // Build the base URL from the remote socket address if it's empty
  if (base_url.empty())
    base_url = "http://" + host;

  // From trailing slash
  if (base_url.back() == '/')
    base_url.pop_back();

  // Remove leading slash
  if (control_url.front() == '/')
    control_url.erase(control_url.begin());

  // Combine base and control URL with slash
  control_url = base_url + "/" + control_url;

  return UPNP::Renderer(uuid, name, control_url);
}

/**
  @brief  Convert a mg_str to std::string
  
  @param  mg_str Mongoose string to convert
  @retval std::string - Empty if mg_str is null
*/
static inline std::string mg_str_string(const mg_str* s)
{
  return (s == nullptr) ? std::string() : std::string(s->p, s->len);
}

/**
  @brief  Mongoose event handler for SSDP description events
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  user_data User data pointer
  @retval none
*/
static void ssdpDescriptionEventHandler(struct mg_connection* nc, int ev, void* ev_data, void* user_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_REPLY:
    {      
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      std::string host = std::string(addr);

      struct http_message* hm = (struct http_message*) ev_data;
      const std::string description = std::string(hm->body.p, hm->body.len);

      ESP_LOGD(TAG, "Description from %s: %s", host.c_str(), description.c_str());

      // Parse the description response into a renderer object
      UPNP::Renderer renderer = SSDP::parse_description(host, description);

      // Ignore invalid objects
      if (!renderer.valid())
        return;

      ESP_LOGD(TAG, "Found renderer: %s - %s", renderer.name.c_str(), renderer.control_url.c_str());

      // Insert renderer into map
      xSemaphoreTake(rendererMutex, portMAX_DELAY);
      
      // Fetch renderer from map and create if needed
      auto it = discoveredRenderers.emplace(renderer.uuid, renderer).first;
      
      // Update name and control URL
      it->second.name = renderer.name;
      it->second.control_url = renderer.control_url;

      xSemaphoreGive(rendererMutex);

      break;
    }

    default:
      break;
  }
}

/**
  @brief  Mongoose event handler for SSDP discovery events
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  user_data User data pointer
  @retval none
*/
static void ssdpDiscoveryEventHandler(struct mg_connection* nc, int ev, void* ev_data, void* user_data)
{
  constexpr uint32_t MG_F_SSDP_SEARCH = MG_F_USER_1;
  constexpr int32_t SSDP_MX = 5;

  // Search target we are looking for
  const char* searchTarget = "urn:schemas-upnp-org:device:MediaRenderer:1";

  // SSDP search request to send
  const char* ssdpSearchRequest =\
  "M-SEARCH * HTTP/1.1\r\n"\
  "HOST: 239.255.255.250:1900\r\n"\
  "MAN: \"ssdp:discover\"\r\n"\
  "ST: %s\r\n"\
  "MX: %d\r\n"\
  "\r\n";

  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      // Only process UDP data
      if ((nc->flags & MG_F_UDP) != MG_F_UDP)
        return;

      struct http_message* hm = (struct http_message *) ev_data;
      
      ESP_LOGD(TAG, "SSDP/HTTP Request: %s", mg_str_string(&hm->message).c_str());

      // Ignore anything but NOTIFY messages
      if (mg_vcasecmp(&hm->method, "NOTIFY") != 0)
        return;
      
      // Ignore advertisements not matching our search target
      struct mg_str* NT = mg_get_http_header(hm, "NT");
      if (mg_vcasecmp(NT, searchTarget) != 0)
        return;

      // Extract NTS field
      struct mg_str* NTS = mg_get_http_header(hm, "NTS");

      // Device is terminating services
      if (mg_vcasecmp(NTS, "ssdp:byebye") == 0)
      {
        // TODO fetch uuid and remove from list
        return;
      }

      // Check that NTS field is alive
      if (mg_vcasecmp(NTS, "ssdp:alive") != 0)
      {
        ESP_LOGW(TAG, "Unrecognized NTS: %s", mg_str_string(NTS).c_str());
        return;
      }

      // Attempt to fetch the interesting fields
      std::string location = mg_str_string(mg_get_http_header(hm, "LOCATION"));
      if (location.empty())
      {
        ESP_LOGE(TAG, "No LOCATION in SSDP NOTIFY.");
        return;
      }

      std::string cache_control = mg_str_string(mg_get_http_header(hm, "CACHE-CONTROL"));
      if (cache_control.empty())
      {
        ESP_LOGE(TAG, "No CACHE-CONTROL in SSDP NOTIFY.");
        return;
      }

      // Sscanf the max-age since std::regex is stack hungry
      uint32_t max_age = 0;
      if (sscanf(cache_control.c_str(), "max-age=%d", &max_age) != 1)
      {
        ESP_LOGE(TAG, "Could not extract max-age from SSDP CACHE-CONTROL: %s", cache_control.c_str());
        return;
      }
      
      if (location.empty() || max_age == 0)
      {
        // Unable to find required fields in response
        ESP_LOGE(TAG, "SSDP NOTIFY missing required fields. Request: %s", mg_str_string(&hm->message).c_str());
        return;
      }

      // Fetch description xml
      mg_connect_http(nc->mgr, ssdpDescriptionEventHandler, nullptr, location.c_str(), nullptr, nullptr);

      break;
    }

    case MG_EV_HTTP_CHUNK:
    {
      // SSDP uses HTTP over UDP. The responses to M-SEARCH do not contain a Content Length header so Mongoose
      // expects the stream to close to indicate completion, and trigger an MG_EV_HTTP_RESPONSE event. However, UDP
      // is connectionless so we only get these MG_EV_HTTP_CHUNK events. Luckily SSDP bodes are always empty so the 
      // response header is enough for us

      // Only process UDP data
      if ((nc->flags & MG_F_UDP) != MG_F_UDP)
        return;
      
      // Ignore data not on the search socket
      if ((nc->flags & MG_F_SSDP_SEARCH) != MG_F_SSDP_SEARCH)
        return;

      struct http_message* hm = (struct http_message *) ev_data;

      ESP_LOGD(TAG, "SSDP/HTTP Response: %s", std::string(hm->message.p, hm->body.p).c_str());
      
      // Ignore bad responses
      if (hm->resp_code != 200)
      {
        ESP_LOGE(TAG, "Invalid SSDP search response: %s", mg_str_string(&hm->resp_status_msg).c_str());
        return;
      }

      // Ignore responses not matching our search target
      struct mg_str* ST = mg_get_http_header(hm, "ST");
      if (mg_vcasecmp(ST, searchTarget) != 0)
      {
        ESP_LOGW(TAG, "Ignoring non-matching ST: %s", mg_str_string(ST).c_str());
        return;
      }

      // Attempt to fetch the interesting fields
      std::string location = mg_str_string(mg_get_http_header(hm, "LOCATION"));
      if (location.empty())
      {
        ESP_LOGE(TAG, "No LOCATION in SSDP search response.");
        return;
      }

      std::string cache_control = mg_str_string(mg_get_http_header(hm, "CACHE-CONTROL"));
      if (cache_control.empty())
      {
        ESP_LOGE(TAG, "No CACHE-CONTROL in SSDP search response.");
        return;
      }

      // Sscanf the max-age since std::regex is stack hungry
      uint32_t max_age = 0;
      if (sscanf(cache_control.c_str(), "max-age=%d", &max_age) != 1)
      {
        ESP_LOGE(TAG, "Could not extract max-age from SSDP CACHE-CONTROL: %s", cache_control.c_str());
        return;
      }

      if (location.empty() || max_age == 0)
      {
        // Unable to find required fields in response
        ESP_LOGE(TAG, "SSDP response missing required fields. Response: %s", std::string(hm->message.p, hm->body.p).c_str());
        return;
      }

      // Fetch description xml
      mg_connect_http(nc->mgr, ssdpDescriptionEventHandler, nullptr, location.c_str(), nullptr, nullptr);

      break;
    }
    
    case MG_EV_TIMER:
    {
      if (nc->flags & MG_F_SSDP_SEARCH)
      {
        // Close the search connection
        ESP_LOGI(TAG, "Search completed.");
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
      }

      // Restart timer to search again
      mg_set_timer(nc, mg_time() + 360);

      ESP_LOGI(TAG, "Sending M-SEARCH.");

      // Create a outbound UDP socket
      struct mg_connection* search = mg_connect(nc->mgr, "udp://239.255.255.250:1900", ssdpDiscoveryEventHandler, nullptr);
      mg_set_protocol_http_websocket(search);

      // Adjust the Multicast TTL of outbound socket to UPnP 1.0 spec
      uint8_t ttl = 4;
      setsockopt(search->sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
      
      // Mark this connection as for searching
      search->flags |= MG_F_SSDP_SEARCH;

      // Send a burst of search requests
      for (uint8_t i = 0; i < 3; i++)
        mg_printf(search, ssdpSearchRequest, searchTarget, SSDP_MX);

      // Stop search after 5 seconds
      mg_set_timer(search, mg_time() + SSDP_MX);

      break;
    }

    default:
      break;
  }
}

/**
  @brief  Main task function of the UPNP control system
  
  @param  pvParameters
  @retval none
*/
void UpnpControl::task(void* pvParameters)
{
  // Flag to indicate if control is enabled
  bool enabled = false;
  
  // Create an event group to run the main loop from
  upnpEventQueue = xQueueCreate(UpnpControl::EVENT_QUEUE_LENGTH, sizeof(UpnpControl::Event));
  if (upnpEventQueue == NULL)
    ESP_LOGE(TAG, "Failed to create event queue.");

  // Create a mutex to lock renderer list access
  rendererMutex = xSemaphoreCreateMutex();
  if (rendererMutex == nullptr)
    ESP_LOGE(TAG, "Failed to create renderer mutex.");

  // Create and init a Mongoose manager
  struct mg_mgr manager;
  mg_mgr_init(&manager, NULL);

  // Bind the SSDP multicast address and port, enable HTTP parsing
  struct mg_connection* ssdp = mg_bind(&manager, "udp://239.255.255.250:1900", ssdpDiscoveryEventHandler, nullptr);
  mg_set_protocol_http_websocket(ssdp);
  
  // Start search shortly
  mg_set_timer(ssdp, mg_time() + 5.0);

  // Join the SSDP multcast group
  ip4_addr_t addr = { .addr = IPADDR_ANY };
  ip4_addr_t groupAddr = { .addr = inet_addr("239.255.255.250") };
  igmp_joingroup(&addr, &groupAddr);

  // Ensure renderers are loaded from NVS
  update_selected_renderers();

  // Loop waiting for events
  while(true)
  {
    mg_mgr_poll(&manager, 1000);

    UpnpControl::Event event;
    if (xQueueReceive(upnpEventQueue, &event, 0) != pdTRUE)
      continue;

    switch (event)
    {
      case Event::Enable:
        enabled = true;
        ESP_LOGI(TAG, "Control enabled.");
        break;

      case Event::Disable:
        enabled = false; 
        ESP_LOGI(TAG, "Control disabled.");
        break;

      case Event::UpdateSelectedRenderers:
      {
        // Update selected renderers
        std::map<std::string, std::string> nvs_renderers = NVS::get_renderers();

        // Lock the renderer list
        xSemaphoreTake(rendererMutex, portMAX_DELAY);

        // Deselect all known renderers
        for (auto& kv : discoveredRenderers)
          kv.second.selected = false;

        // Select all renderers saved in NVS
        for (const auto& kv : nvs_renderers)
        {
          const std::string& uuid = kv.first;
          const std::string& name = kv.second;

          auto it = discoveredRenderers.emplace(uuid, UPNP::Renderer(uuid, name)).first;
          it->second.selected = true;

          ESP_LOGI(TAG, "Selected '%s' for playback.", it->second.name.c_str());
        }

        xSemaphoreGive(rendererMutex);
        break;
      }

      case Event::SendPlayAction:
      {
        if (!enabled)
          break;

        // Start playback on selected renderers

        // Build URI for the stream
        tcpip_adapter_ip_info_t info;
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &info);

        std::string uri = "http://" + std::string(ip4addr_ntoa(&info.ip)) + "/stream.wav";

        // Mongoose handler to chain a Play action on success
        auto event_handler = [](struct mg_connection* nc, int ev, void* ev_data, void* user_data)
        {
          if (ev != MG_EV_HTTP_REPLY)
            return;
        
          struct http_message* hm = (struct http_message*) ev_data;
            
          // Throw error on bad response
          if (hm->resp_code != 200)
          {
            ESP_LOGE(TAG, "Failed SetAvTransportUri action. Code: %d Response: %s.", hm->resp_code, mg_str_string(&hm->resp_status_msg).c_str());
            return;
          }

          // Mongoose handler for play action
          auto play_event_handler = [](struct mg_connection* nc, int ev, void* ev_data, void* user_data)
          {
            if (ev != MG_EV_HTTP_REPLY)
              return;
            
            struct http_message* hm = (struct http_message*) ev_data;
            
            if (hm->resp_code != 200)
              ESP_LOGE(TAG, "Failed Play action. Code: %d Response: %s.", hm->resp_code, mg_str_string(&hm->resp_status_msg).c_str());
          };

          // Extact the renderer control url from the user_data pointer
          const std::string* url = (const std::string*) user_data;
          
          // Send play action to renderer
          UPNP::PlayAction play;
          mg_connect_http(nc->mgr, play_event_handler, nullptr, url->c_str(), play.headers().c_str(), play.body().c_str());

          // Free the control url
          // TODO we could leak memory if we never get a reply
          // Maybe free this on CLOSE instead
          delete url;
        };

        xSemaphoreTake(rendererMutex, portMAX_DELAY);

        for (const auto& kv : discoveredRenderers)
        {
          const UPNP::Renderer& r = kv.second;
          
          // Ignore non-selected renderers
          if (r.selected == false)
            continue;
          
          // Send command if we have a valid control URL
          if (r.control_url.empty())
          {
            ESP_LOGW(TAG, "No control URL for '%s'.", r.name.c_str());
            continue;
          }

          ESP_LOGI(TAG, "Starting playback on '%s'.", r.name.c_str());

          // Allocate a string on the heap to pass as user_data
          std::string* url = new std::string(r.control_url);

          // Construct and send the set URI action
          UPNP::SetAvTransportUriAction setUri(uri);
          mg_connect_http(&manager, event_handler, url, r.control_url.c_str(), setUri.headers().c_str(), setUri.body().c_str());
        }

        xSemaphoreGive(rendererMutex);
        break;
      }

      case Event::SendStopAction:
      {
        if (!enabled)
          break;

        // Stop playback on selected renderers
        
        // Mongoose handler. Yay lambdas
        auto event_handler = [](struct mg_connection* nc, int ev, void* ev_data, void* user_data)
        {
          if (ev != MG_EV_HTTP_REPLY)
            return;
          
          struct http_message* hm = (struct http_message*) ev_data;
          
          if (hm->resp_code != 200)
            ESP_LOGE(TAG, "Failed Stop action. Code: %d Response: %s.", hm->resp_code, mg_str_string(&hm->resp_status_msg).c_str());
        };

        xSemaphoreTake(rendererMutex, portMAX_DELAY);

        for (const auto& kv : discoveredRenderers)
        {
          const UPNP::Renderer& r = kv.second;
    
          // Ignore non-selected renderers
          if (r.selected == false)
            continue;

          if (r.control_url.empty())
          {
            ESP_LOGW(TAG, "No control URL for '%s'.", r.name.c_str());
            continue;
          }

          ESP_LOGI(TAG, "Stopping playback on '%s'.", r.name.c_str());

          // Send stop action to renderer
          UPNP::StopAction stop;
          mg_connect_http(&manager, event_handler, nullptr, r.control_url.c_str(), stop.headers().c_str(), stop.body().c_str());
        }

        xSemaphoreGive(rendererMutex);
        break;
      }

      default:
        break;
    }
  }

  // Free the manager if we ever exit
  mg_mgr_free(&manager);

  vTaskDelete(NULL);
}

/**
  @brief  Queue an event on the UpnpControl queue
  
  @param  event Event to queue
  @retval none
*/
static void queue_event(UpnpControl::Event event)
{
  if (upnpEventQueue != NULL)
    xQueueSendToBack(upnpEventQueue, &event, pdMS_TO_TICKS(10));
}

/**
  @brief  Send a play command to the renderer
  
  @param  none
  @retval none
*/
void UpnpControl::enable()
{
  queue_event(Event::Enable);
  queue_event(Event::SendPlayAction);
}

/**
  @brief  Send a stop command to the renderer
  
  @param  none
  @retval none
*/
void UpnpControl::disable()
{
  queue_event(Event::SendStopAction);
  queue_event(Event::Disable);
}

/**
  @brief  Update the renderers selected for playback
  
  @param  none
  @retval none
*/
void UpnpControl::update_selected_renderers()
{
  // Stop existing renderers
  queue_event(Event::SendStopAction);

  // Update renderers
  queue_event(Event::UpdateSelectedRenderers);

  // Resume playback on new renderers
  queue_event(Event::SendPlayAction);
}

/**
  @brief  Fetch the list of known renderers
  
  @param  none
  @retval renderer_map_t Map of known renderers
*/
UpnpControl::renderer_map_t UpnpControl::get_known_renderers()
{
  xSemaphoreTake(rendererMutex, portMAX_DELAY);
  
  // Make a copy
  renderer_map_t renderers = discoveredRenderers;
  
  xSemaphoreGive(rendererMutex);

  return renderers;
}