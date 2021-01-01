#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

#include <string>

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