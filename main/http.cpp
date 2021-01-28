#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include <unordered_set>

#include "http.h"
#include "i2s_interface.h"
#include "json.h"
#include "mongoose.h"
#include "utils.h"
#include "wav.h"

#define TAG "HTTP"

static std::unordered_set<struct mg_connection*> clients;
static SemaphoreHandle_t clientMutex;

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
        ESP_LOGW(TAG, "Client %p (%s) already exists.", nc, addr);
        xSemaphoreGive(clientMutex);
        return;
      }

      // Construct a queue for this client
      QueueHandle_t queue = xQueueCreate(HTTP::CLIENT_QUEUE_LENGTH, sizeof(I2S::sample_buffer_t));
      if (queue == nullptr)
      {
        ESP_LOGE(TAG, "Failed to create queue for client %p (%s).", nc, addr);
        xSemaphoreGive(clientMutex);
        return;
      }

      nc->user_data = queue;
      clients.insert(nc);
      xSemaphoreGive(clientMutex);

      // Grab the stream object from the user_data
      HTTP::StreamConfig* streamConfig = (HTTP::StreamConfig*) user_data;
      assert(streamConfig != nullptr);

      ESP_LOGI(TAG, "New %s client %p (%s).", streamConfig->name, nc, addr);

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
      // Get queue handle from the connection
      QueueHandle_t queue = (QueueHandle_t) nc->user_data;
      assert(queue != nullptr);
  
      I2S::sample_buffer_t samples;
      while (xQueueReceive(queue, samples.data(), 0) == pdTRUE)
        mg_send(nc, samples.data(), sizeof(samples));
  
      break;
    }

    case MG_EV_POLL:
    {
      // If send buffer if empty, attempt to start sending data
      if (nc->send_mbuf.len == 0)
      {
        // Get queue handle from the connection
        QueueHandle_t queue = (QueueHandle_t) nc->user_data;
        assert(queue != nullptr);
    
        I2S::sample_buffer_t samples;
        while (xQueueReceive(queue, samples.data(), 0) == pdTRUE)
          mg_send(nc, samples.data(), sizeof(samples));
      }
      break;
    }

    case MG_EV_CLOSE:
    {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      ESP_LOGI(TAG, "Client %p (%s) disconnected.", nc, addr);

      // Delete the clients queue
      if (nc->user_data != nullptr)
        vQueueDelete((QueueHandle_t) nc->user_data);
      else
        ESP_LOGE(TAG, "No queue for client %p (%s).", nc, addr);

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
  extern const uint8_t index_html[] asm("_binary_index_html_start");
  extern const uint8_t index_html_end[] asm("_binary_index_html_end");
  const uint32_t index_html_len = index_html_end - index_html;

  switch(ev)
  {
    case MG_EV_HTTP_REQUEST:
    {
      struct http_message *hm = (struct http_message *) ev_data;
      
      char action[4];
      if (mg_get_http_var(&hm->query_string, "action", action, sizeof(action)) == -1)
      {
        // Send the header
        mg_send_head(nc, 200, index_html_len, "Content-Type: text/html");

        // Serve the page
        mg_send(nc, index_html, index_html_len);
        nc->flags |= MG_F_SEND_AND_CLOSE;
        break;
      }
      else if (strcmp(action, "get") == 0) // Get JSON values
      {
        std::string renderers = JSON::get_renderers();

        ESP_LOGI(TAG, "Get = %s", renderers.c_str());

        mg_send_head(nc, 200, renderers.length(), "Content-Type: application/json");
        mg_send(nc, renderers.c_str(), renderers.length());
        nc->flags |= MG_F_SEND_AND_CLOSE;
    
        break;
      }
      else if (strcmp(action, "set") == 0) // Set JSON values
      {
        // Move JSON data into null terminated buffer
        std::string buffer(hm->body.p, hm->body.p + hm->body.len);

        ESP_LOGI(TAG, "Set = %s", buffer.c_str());

        bool success = JSON::parse_renderers(buffer);

        const char* errorString = success ? "Update successful." : "JSON parse failed.";
        
        mg_send_head(nc, success ? 200 : 400, strlen(errorString), "Content-Type: text/html");
        mg_printf(nc, errorString);
        nc->flags |= MG_F_SEND_AND_CLOSE;
      }
      else
      {
        mg_http_send_redirect(nc, 302, hm->uri, mg_mk_str(NULL));
        nc->flags |= MG_F_SEND_AND_CLOSE;
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

  // Add separate end points for raw PCM and WAV stream
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
  
  @param  samples Buffer to enqueue
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