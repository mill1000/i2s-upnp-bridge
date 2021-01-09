#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "lwip/igmp.h"

#include <string>
#include <regex>

#include "upnp_control.h"
#include "upnp.h"
#include "mongoose.h"
#include "utils.h"

#define TAG "UPNP"

static EventGroupHandle_t upnpEventGroup;

/**
  @brief  Generic Mongoose event handler for the HTTP server
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  user_data User data pointer
  @retval none
*/
static void httpEventHandler(struct mg_connection* nc, int ev, void* ev_data, void* user_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_REPLY:
    {
      struct http_message* hm = (struct http_message*) ev_data;
      
      UpnpControl::event_callback_t callback = (UpnpControl::event_callback_t) user_data;
      if (callback)
        callback(hm->resp_code, std::string(hm->resp_status_msg.p, hm->resp_status_msg.len));
    
      break;
    }

    default:
      break;
  }
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
      struct http_message* hm = (struct http_message*) ev_data;;
      
      ESP_LOGD(TAG, "Description Reply: %s", std::string(hm->message.p, hm->message.len).c_str());

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

  //ST: upnp:rootdevice
  //ST: urn:schemas-upnp-org:device:MediaRenderer:1
  const char* searchTarget = "urn:schemas-upnp-org:device:MediaRenderer:1";
  const char* ssdpSearchRequest =\
  "M-SEARCH * HTTP/1.1\r\n"\
  "HOST: 239.255.255.250:1900\r\n"\
  "MAN: \"ssdp:discover\"\r\n"\
  "ST: %s\r\n"\
  "MX: %d\r\n"\
  "\r\n";
  
  // Construct a std::string from a mg_str
  auto mg_str_string = [](const mg_str* s){
    return (s == nullptr) ? std::string() : std::string(s->p, s->len);
  };

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

      // Fetch max-age from CACHE-CONTROL field
      std::smatch age_match;
      if (std::regex_match(cache_control, age_match, std::regex(R"(max-age=(\d+))")) == false)
      {
        ESP_LOGE(TAG, "Could not extract max-age from SSDP CACHE-CONTROL: %s", cache_control.c_str());
        return;
      }

      if (location.empty() || age_match.empty())
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

      // Fetch max-age from CACHE-CONTROL field
      std::smatch age_match;
      if (std::regex_match(cache_control, age_match, std::regex(R"(max-age=(\d+))")) == false)
      {
        ESP_LOGE(TAG, "Could not extract max-age from SSDP CACHE-CONTROL: %s", cache_control.c_str());
        return;
      }

      // std::string usn = mg_str_string(mg_get_http_header(hm, "USN"));
      // if (usn.empty())
      // {
      //   ESP_LOGE(TAG, "No USN in SSDP search response.");
      //   return;
      // }

      // // Fetch UUID from USN field
      // std::smatch uuid_match;
      // if (std::regex_match(usn, uuid_match, std::regex("uuid:(.*?)::urn:.*")) == false)
      // {
      //   ESP_LOGE(TAG, "Could not extract UUID from SSDP USN: %s", usn.c_str());
      //   return;
      // }

      if (location.empty() || age_match.empty())
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
      mg_set_timer(nc, mg_time() + 60);

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
  // Create an event group to run the main loop from
  upnpEventGroup = xEventGroupCreate();
  if (upnpEventGroup == NULL)
    ESP_LOGE(TAG, "Failed to create event group.");

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

  const char* url = "http://192.168.1.104:1150/AVTransport/33aceb4c-6ebd-62eb-2f4c-fd5298f56d43/control.xml";
  //const char* url = "http://192.168.1.3:49494/upnp/control/rendertransport1";
  // Loop waiting for events
  while(true)
  {
    EventBits_t events = xEventGroupWaitBits(upnpEventGroup, (uint32_t) Event::All, pdTRUE, pdFALSE, 0);

    // Start playback by first setting the URI
    if (events & (uint32_t) Event::Play)
      xEventGroupSetBits(upnpEventGroup, (uint32_t) Event::SendSetUriAction);

    // Stop playback by sending the Stop action
    if (events & (uint32_t) Event::Stop)
      xEventGroupSetBits(upnpEventGroup, (uint32_t) Event::SendStopAction);

    if (events & (uint32_t) Event::SendSetUriAction)
    {
      tcpip_adapter_ip_info_t info;
      tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &info);

      std::string uri = "http://" + std::string(ip4addr_ntoa(&info.ip)) + "/stream.wav";
      ESP_LOGI(TAG, "SetAvTransportUri = '%s'", uri.c_str());

      // Create the set URI action
      UPNP::SetAvTransportUriAction setUri(uri);

      event_callback_t callback = [](int code, const std::string& result)
      {
        // Follow will a Play action if successful
        if (code == 200)
          xEventGroupSetBits(upnpEventGroup, (uint32_t) Event::SendPlayAction);
        else
          ESP_LOGE(TAG, "Failed SetAvTransportUri action. Code: %d Response: %s.", code, result.c_str());
      };

      mg_connect_http(&manager, httpEventHandler, (void*)callback, url, setUri.headers().c_str(), setUri.body().c_str());
    }

    if (events & (uint32_t) Event::SendPlayAction)
    {
      // Start playback
      UPNP::PlayAction play;

      event_callback_t callback = [](int code, const std::string& result)
      {
        if (code != 200)
          ESP_LOGE(TAG, "Failed Play action. Code: %d Response: %s.", code, result.c_str());
      };

      mg_connect_http(&manager, httpEventHandler, (void*)callback, url, play.headers().c_str(), play.body().c_str());
    }

    if (events & (uint32_t) Event::SendStopAction)
    {
      // Stop playback
      UPNP::StopAction stop;

      event_callback_t callback = [](int code, const std::string& result)
      {
        if (code != 200)
          ESP_LOGE(TAG, "Failed Stop action. Code: %d Response: %s.", code, result.c_str());
      };

      mg_connect_http(&manager, httpEventHandler, (void*)callback, url, stop.headers().c_str(), stop.body().c_str());
    }

    mg_mgr_poll(&manager, 1000);
  }

  // Free the manager if we ever exit
  mg_mgr_free(&manager);

  vTaskDelete(NULL);
}

/**
  @brief  Send a play command to the renderer
  
  @param  none
  @retval none
*/
void UpnpControl::play()
{
  if (upnpEventGroup != NULL)
    xEventGroupSetBits(upnpEventGroup, (uint32_t) Event::Play);
}

/**
  @brief  Send a stop command to the renderer
  
  @param  none
  @retval none
*/
void UpnpControl::stop()
{
  if (upnpEventGroup != NULL)
    xEventGroupSetBits(upnpEventGroup, (uint32_t) Event::Stop);
}