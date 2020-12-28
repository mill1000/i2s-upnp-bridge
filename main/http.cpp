#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i2s_interface.h"

#include "http.h"
#include "mongoose.h"

#define TAG "HTTP"

int16_t samples[2 * I2S::BUFFER_SAMPLE_COUNT];

/**
  @brief  Generic Mongoose event handler for the HTTP server
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @retval none
*/
static void httpEventHandler(struct mg_connection* nc, int ev, void* ev_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      // Send the header. Shamelessly stolen from what AirAudio sends
      mg_send_response_line(nc, 200, "Content-Type: audio/L16;rate=48000;channels=2\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0\r\n");
    
      // Reassign event handler for this client so MG_EV_SEND will fire in this function
      nc->handler = httpEventHandler;
      ESP_LOGI(TAG, "Sending");
      break;
    }

    case MG_EV_SEND:
    {

      // Read as much data as possible from the I2S interface
      size_t total = 0;
      while (true)
      {
        size_t read = I2S::read(samples, sizeof(samples), pdMS_TO_TICKS(200));
        mg_send(nc, samples, read);

        total += read;

        if (read != sizeof(samples))
        {
          ESP_LOGI(TAG, "Only read %d of %d bytes", read, sizeof(samples));
          break; // I2S low on data
        }

        if (total > 1024)
        {
          //ESP_LOGI(TAG, "full");
          break; // I2S low on data
        }


        // TODO there is probably some condition we should close the connection under
      }
      
      break;
    }

    default:
      break;
  }
}

/**
  @brief  Main task function of the HTTP server
  
  @param  pvParameters
  @retval none
*/
void HTTP::task(void* pvParameters)
{
  ESP_LOGI(TAG, "Starting HTTP server...");

  // Create and init the event manager
  struct mg_mgr manager;
  mg_mgr_init(&manager, NULL);

  // Connect bind to an address and specify the event handler
  struct mg_connection* connection = mg_bind(&manager, "80", httpEventHandler);
  if (connection == NULL)
  {
    ESP_LOGE(TAG, "Failed to bind port.");
    mg_mgr_free(&manager);
    vTaskDelete(NULL);
    return;
  }

  // Enable HTTP on the connection
  mg_set_protocol_http_websocket(connection);

  // Loop waiting for events
  while(1)
    mg_mgr_poll(&manager, 1000);

  // Free the manager if we ever exit
  mg_mgr_free(&manager);

  vTaskDelete(NULL);
}