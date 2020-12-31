#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include <unordered_set>

#include "http.h"
#include "mongoose.h"
#include "i2s_interface.h"
#include "utils.h"
#include "wav.h"

#define TAG "HTTP"

static std::unordered_set<struct mg_connection*> clients;
static SemaphoreHandle_t clientMutex;

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
  @brief  Mongoose event handler to stream audio data to clients
  
  @param  nc Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  user_data User data pointer
  @retval none
*/
static void httpStreamEventHandler(struct mg_connection* nc, int ev, void* ev_data, void* user_data)
{
  // Lambda to fill outbound client buffer from it's associated queue
  auto fill_client_buffer = [nc]()
  {
    // Get queue handle from the connection
    QueueHandle_t queue = (QueueHandle_t) nc->user_data;
    assert(queue != nullptr);
    
    // Fill up the outgoing buffer
    while (nc->send_mbuf.len < HTTP::CLIENT_MAX_SEND_BUFFER_LENGTH)
    {
      I2S::sample_buffer_t samples;
      if (xQueueReceive(queue, samples.data(), 0) != pdTRUE)
        break;
      
      mg_send(nc, samples.data(), sizeof(samples));
    }
  };

  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);

      // Lock the client list
      xSemaphoreTake(clientMutex, portMAX_DELAY);

      if (clients.count(nc))
      {
        ESP_LOGW(TAG, "Client %s already exists.", addr);
        xSemaphoreGive(clientMutex);
        return;
      }

      // Construct a queue for this client
      QueueHandle_t queue = xQueueCreate(HTTP::CLIENT_QUEUE_LENGTH, sizeof(I2S::sample_buffer_t));
      if (queue == nullptr)
      {
        ESP_LOGE(TAG, "Failed to create queue for client %s.", addr);
        xSemaphoreGive(clientMutex);
        return;
      }

      nc->user_data = queue;
      clients.insert(nc);
      xSemaphoreGive(clientMutex);

      // Grab the stream object from the user_data
      HTTP::StreamConfig* streamConfig = (HTTP::StreamConfig*) user_data;
      assert(streamConfig != nullptr);

      ESP_LOGI(TAG, "New %s client %s -> %p.", streamConfig->name, addr, nc);

      // Send the HTTP header
      mg_send_response_line(nc, 200, streamConfig->headers);

      // Perform additional setup if needed
      if (streamConfig->setup)
        streamConfig->setup(nc);

      // Reassign event handler for this client
      nc->handler = httpStreamEventHandler;

      break;
    }

    case MG_EV_SEND:
    {
      fill_client_buffer();
      break;
    }

    case MG_EV_POLL:
    {
      // If send buffer if empty, attempt to start sending data
      if (nc->send_mbuf.len == 0)
        fill_client_buffer();
      break;
    }

    case MG_EV_CLOSE:
    {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "Client %s disconnected", addr);

      // Delete the clients queue
      if (nc->user_data != nullptr)
        vQueueDelete((QueueHandle_t) nc->user_data);
      else
        ESP_LOGE(TAG, "No queue for client %s.", addr);

      // Remove the client
      xSemaphoreTake(clientMutex, portMAX_DELAY);
      clients.erase(nc);
      xSemaphoreGive(clientMutex);

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
  @param  user_data User data pointer
  @retval none
*/
static void httpEventHandler(struct mg_connection* nc, int ev, void* ev_data, void* user_data)
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

  clientMutex = xSemaphoreCreateMutex();
  if (clientMutex == nullptr)
  {
    ESP_LOGE(TAG, "Failed to create client mutex.");
    return;
  }

  // Create and init the event manager
  struct mg_mgr manager;
  mg_mgr_init(&manager, NULL);

  // Connect bind to an address and specify the event handler
  struct mg_connection* connection = mg_bind(&manager, "80", httpEventHandler, nullptr);
  if (connection == NULL)
  {
    ESP_LOGE(TAG, "Failed to bind port.");
    mg_mgr_free(&manager);
    vTaskDelete(NULL);
    return;
  }

  // Enable HTTP on the connection
  mg_set_protocol_http_websocket(connection);

  // Construct the PCM stream object
  StreamConfig pcm("PCM", "Content-Type: audio/L16;rate=48000;channels=2\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0\r\n");

  // Construct the WAV stream object
  StreamConfig wav("WAV", "Content-Type: audio/wav\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0\r\n");
  wav.setup = [](struct mg_connection* nc)
  {
    // Construct and send the WAV header
    WAV::Header wav_header(48000);
    mg_send(nc, &wav_header, sizeof(wav_header));
  };

  // Add seperate end points for raw PCM and WAV stream
  mg_register_http_endpoint(connection, "/stream.pcm", httpStreamEventHandler, &pcm);
  mg_register_http_endpoint(connection, "/stream.wav", httpStreamEventHandler, &wav);

  // Loop waiting for events
  while(1)
    mg_mgr_poll(&manager, 10);

  // Free the manager if we ever exit
  mg_mgr_free(&manager);

  vTaskDelete(NULL);
}

/**
  @brief  Queue sample data for transmission to clients
  
  @param  samples Buffer to enque
  @retval none
*/
void HTTP::queue_samples(const I2S::sample_buffer_t& samples)
{
  // Lock the client list
  if (xSemaphoreTake(clientMutex, pdMS_TO_TICKS(200)) != pdTRUE)
  {
    ESP_LOGE(TAG, "Failed to lock client mutex.");
    return;
  }

  for (const auto nc : clients)
  {
    // Get queue handle from the connection
    QueueHandle_t queue = (QueueHandle_t) nc->user_data;
    assert(queue != nullptr);

    // Attempt to queue the sample data
    if (xQueueSendToBack(queue, samples.data(), pdMS_TO_TICKS(50)) == pdTRUE)
      continue;
    
    ESP_LOGW(TAG, "Client %p queue overflow.", nc);

    // Free up space in the queue
    I2S::sample_buffer_t dummy;
    if (xQueueReceive(queue, dummy.data(), pdMS_TO_TICKS(50)) != pdTRUE)
      ESP_LOGE(TAG, "Failed to pop from full queue.");

    // Queue without blocking this time
    if (xQueueSendToBack(queue, samples.data(), 0) != pdTRUE)
      ESP_LOGE(TAG, "Failed to queue samples for %p.", nc);
  }

  xSemaphoreGive(clientMutex);
}