#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "i2s_interface.h"

#include "http.h"
#include "mongoose.h"
#include "utils.h"
#include "wav.h"

#define TAG "HTTP"

/**
  @brief  Sends HTTP responses on the provided connection
  
  @param  nc Mongoose connection
  @param  code HTTP event code to send
  @param  message HTTP response body to send
  @retval none
*/
static void httpSendResponse(struct mg_connection* nc, uint16_t code, const char* message)
{
  mg_send_head(nc, code, strlen(message), "Content-Type: text/html");
  mg_printf(nc, message);
  nc->flags |= MG_F_SEND_AND_CLOSE;
}

/**
  @brief  Mongoose event handler to stream PCM data
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @retval none
*/
static void streamEventHandler(struct mg_connection* nc, int ev, void* ev_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      // We don't expect HTTP requests on this handler
      httpSendResponse(nc, 500, "Unhandled request.");
      break;
    }

    case MG_EV_SEND:
    {
      I2S::sample_t samples[2 * I2S::BUFFER_SAMPLE_COUNT];

      // Fill up the outgoing buffer
      while (nc->send_mbuf.len < HTTP::MAX_SEND_BUFFER_LENGTH)
      {
        // Calculate the number of bytes to read
        size_t length = Utils::min(HTTP::MAX_SEND_BUFFER_LENGTH - nc->send_mbuf.len, sizeof(samples));

        // Round length down to multiples of a sample
        length = length - length % sizeof(I2S::sample_t);
        assert(length % sizeof(I2S::sample_t) == 0);

        size_t read = I2S::read(samples, length, pdMS_TO_TICKS(200));
        mg_send(nc, samples, read);

        // We'll be in trouble if we read something smaller than a sample
        assert(read % sizeof(I2S::sample_t) == 0);

        if (read < length)
          break; // I2S low on data

        // TODO there is probably some condition we should close the connection under
      }

      break;
    }

    case MG_EV_CLOSE:
    {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "Disconnected from %s.", addr);

      break;
    }

    default:
      break;
  }
}

/**
  @brief  Mongoose event handler for the WAV endpoint
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @retval none
*/
static void wavStreamEventHandler(struct mg_connection* nc, int ev, void* ev_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "New WAV request from %s.", addr);

      // Send the HTTP header
      mg_send_response_line(nc, 200, "Content-Type: audio/wav\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0\r\n");

      // Construct and send the WAV header
      WAV::Header wav_header(48000);
      mg_send(nc, &wav_header, sizeof(wav_header));

      // Reassign event handler for this client
      nc->handler = streamEventHandler;

      break;
    }

    default:
      break;
  }
}

/**
  @brief  Mongoose event handler for the raw PCM endpoint
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @retval none
*/
static void pcmStreamEventHandler(struct mg_connection* nc, int ev, void* ev_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "New PCM request from %s.", addr);

      // Send the header. Shamelessly stolen from what AirAudio sends
      mg_send_response_line(nc, 200, "Content-Type: audio/L16;rate=48000;channels=2\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0 \r\n");

      // Adding a byte of padding makes foobar happy?
      // while (nc->send_mbuf.len % 2 != 1)
      // {
      //   uint8_t zero = 0;
      //   mg_send(nc, &zero, sizeof(zero));
      // }

      // Reassign event handler for this client
      nc->handler = streamEventHandler;

      break;
    }

    default:
      break;
  }
}

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
      httpSendResponse(nc, 200, "OK");
      break;

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
  ESP_LOGI(TAG, "Starting HTTP server.");

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

  // Add seperate end points for raw PCM and WAV stream
  mg_register_http_endpoint(connection, "/stream.pcm", pcmStreamEventHandler);
  mg_register_http_endpoint(connection, "/stream.wav", wavStreamEventHandler);

  // Loop waiting for events
  while(1)
    mg_mgr_poll(&manager, 1000);

  // Free the manager if we ever exit
  mg_mgr_free(&manager);

  vTaskDelete(NULL);
}