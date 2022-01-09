#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include <queue>
#include <unordered_set>

#include "http.h"
#include "i2s_interface.h"
#include "json.h"
#include "mongoose.h"
#include "warthog.h"
#include "ota_interface.h"
#include "system.h"
#include "wav.h"

#define TAG "HTTP"

static std::unordered_set<struct mg_connection*> clients;
static SemaphoreHandle_t client_mutex;

/**
  @brief  Mongoose event handler to stream audio data to clients
  
  @param  c Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  fn_data Function data pointer
  @retval none
*/
static void httpStreamEventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data)
{
  switch(ev)
  {
    case MG_EV_HTTP_MSG:
    {
      char addr[32];
      mg_straddr(&c->peer, addr, sizeof(addr));

      // Lock the client list
      xSemaphoreTake(client_mutex, portMAX_DELAY);

      if (clients.count(c))
      {
        ESP_LOGW(TAG, "Client %p (%s) already exists.", c, addr);
        xSemaphoreGive(client_mutex);
        return;
      }

      // Construct a queue for this client
      QueueHandle_t queue = xQueueCreate(HTTP::CLIENT_QUEUE_LENGTH, sizeof(I2S::sample_buffer_t));
      if (queue == nullptr)
      {
        ESP_LOGE(TAG, "Failed to create queue for client %p (%s).", c, addr);
        xSemaphoreGive(client_mutex);
        return;
      }

      c->fn_data = queue;
      clients.insert(c);

      // Notify system of first client
      if (clients.size() == 1)
        System::set_active_state();

      xSemaphoreGive(client_mutex);

      // Grab the stream object from the fn_data
      HTTP::StreamConfig* stream_config = (HTTP::StreamConfig*) fn_data;
      assert(stream_config != nullptr);

      ESP_LOGI(TAG, "New %s client %p (%s).", stream_config->name, c, addr);

      // Send the HTTP header
      mg_http_reply(c, 200, stream_config->headers, "");

      // Perform additional setup if needed
      if (stream_config->setup)
        stream_config->setup(c);

      // Reassign event handler for this client
      c->fn = httpStreamEventHandler;

      break;
    }

    case MG_EV_WRITE:
    case MG_EV_POLL:
    {
      // Service queues on every poll or send event

      // Get queue handle from the connection
      QueueHandle_t queue = (QueueHandle_t) c->fn_data;
      assert(queue != nullptr);
  
      I2S::sample_buffer_t samples;
      while (xQueueReceive(queue, samples.data(), 0) == pdTRUE)
        mg_send(c, samples.data(), sizeof(samples));

      break;
    }

    case MG_EV_CLOSE:
    {
      char addr[32];
      mg_straddr(&c->peer, addr, sizeof(addr));
      ESP_LOGI(TAG, "Client %p (%s) disconnected.", c, addr);

      // Delete the clients queue
      if (c->fn_data != nullptr)
        vQueueDelete((QueueHandle_t) c->fn_data);
      else
        ESP_LOGE(TAG, "No queue for client %p (%s).", c, addr);

      // Remove the client
      xSemaphoreTake(client_mutex, portMAX_DELAY);
      clients.erase(c);
      
      // Notify system of last client
      if (clients.empty())
        System::set_idle_state();

      xSemaphoreGive(client_mutex);

      break;
    }

    default:
      break;
  }
}

/**
  @brief  Generic Mongoose event handler for the HTTP server
  
  @param  c Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  fn_data Function data pointer
  @retval none
*/
static void httpEventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data)
{
  extern const char index_html[] asm("_binary_index_html_start");

  switch(ev)
  {
    case MG_EV_HTTP_MSG:
    {
      struct mg_http_message *hm = (struct mg_http_message*) ev_data;
      
      char action[4];
      if (mg_http_get_var(&hm->query, "action", action, sizeof(action)) == -1)
      {
        // Serve the page
        mg_http_reply(c, 200, "Content-Type: text/html\r\n", index_html);
        c->is_draining = true;
        break;
      }
      else if (strcmp(action, "get") == 0) // Get JSON values
      {
        std::string renderers = JSON::get_renderers();

        ESP_LOGI(TAG, "Get = %s", renderers.c_str());

        mg_http_reply(c, 200, "Content-Type: application/json\r\n", renderers.c_str());
        c->is_draining = true;
    
        break;
      }
      else if (strcmp(action, "set") == 0) // Set JSON values
      {
        // Move JSON data into null terminated buffer
        std::string buffer(hm->body.ptr, hm->body.ptr + hm->body.len);

        ESP_LOGI(TAG, "Set = %s", buffer.c_str());

        bool success = JSON::parse_renderers(buffer);

        const char* error_string = success ? "Update successful." : "JSON parse failed.";
        
        mg_http_reply(c, success ? 200 : 400, "Content-Type: text/html\r\n", error_string);
        c->is_draining = true;
      }
      else
      {
        Warthog::http_send_redirect(c, 302, "/");
        c->is_draining = true;
      }
      break;
    }

    default:
      break;
  }
}

/**
  @brief  Mongoose event handler for the OTA firmware update
  
  @param  c Mongoose connection
  @param  ev Mongoose event calling the function
  @param  ev_data Event data pointer
  @param  fn_data Function data pointer
  @retval none
*/
// static void otaEventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data)
// {
//   extern const uint8_t ota_html[] asm("_binary_ota_html_start");
//   extern const uint8_t ota_html_end[] asm("_binary_ota_html_end");
//   const uint32_t ota_html_len = ota_html_end - ota_html;

//   constexpr uint32_t MG_F_OTA_FAILED = MG_F_USER_1;
//   constexpr uint32_t MG_F_OTA_COMPLETE = MG_F_USER_2;

//   static std::queue<OTA::end_callback_t> callbacks;
//   static std::string response;

//   switch(ev)
//   {
//     case MG_EV_HTTP_MSG:
//     {
//       // Send the header
//       mg_send_head(c, 200, ota_html_len, "Content-Type: text/html");

//       // Serve the page
//       mg_send(c, ota_html, ota_html_len);
//       c->is_draining= true;
//       break;
//     }

//     case MG_EV_HTTP_MULTIPART_REQUEST:
//     {
//       // Reset response string
//       response.clear();
//       break;
//     }

//     case MG_EV_HTTP_PART_BEGIN:
//     {
//       struct mg_http_multipart_part* multipart = (struct mg_http_multipart_part*) ev_data;

//       if (multipart->fn_data != nullptr)
//       {
//         ESP_LOGE(TAG, "Non-null OTA handle. OTA already in progress?");

//         // Mark OTA as failed and append reason
//         response += "OTA update already in progress.";
//         c->flags |= MG_F_OTA_FAILED;
//         return;
//       }

//       // Construct new OTA handle for application
//       OTA::Handle* ota = new OTA::AppHandle();
//       if (ota == nullptr)
//       {
//         ESP_LOGE(TAG, "Failed to construct OTA handle.");

//         // Mark OTA as failed and append reason
//         response += "OTA update init failed.";
//         c->flags |= MG_F_OTA_FAILED;
//         return;
//       }

//       ESP_LOGI(TAG, "Starting OTA...");
//       esp_err_t result = ota->start();
//       if (result != ESP_OK)
//       {
//         ESP_LOGE(TAG, "Failed to start OTA. Error: %s", esp_err_to_name(result));

//         // Mark OTA as failed and append reason
//         response += "OTA update init failed.";
//         c->flags |= MG_F_OTA_FAILED;

//         // Kill the handle
//         delete ota;
//         multipart->fn_data = nullptr;
//         return;
//       }

//       // Save the handle for reference in future calls
//       multipart->fn_data = (void*) ota;
//       break;
//     }

//     case MG_EV_HTTP_PART_DATA:
//     {
//       struct mg_http_multipart_part* multipart = (struct mg_http_multipart_part*) ev_data;

//       // Something went wrong so ignore the data
//       if (c->flags & MG_F_OTA_FAILED)
//         return;

//       // Fetch handle from fn_data
//       OTA::Handle* ota = (OTA::Handle*) multipart->fn_data;
//       if (ota == nullptr)
//         return;

//       if (ota->write((uint8_t*) multipart->data.p, multipart->data.len) != ESP_OK)
//       {
//         // Mark OTA as failed and append reason
//         response += "OTA write failed.";
//         c->flags |= MG_F_OTA_FAILED;
//       }
//       break;
//     }

//     case MG_EV_HTTP_PART_END:
//     {
//       struct mg_http_multipart_part* multipart = (struct mg_http_multipart_part*) ev_data;
      
//       // Fetch handle from fn_data
//       OTA::Handle* ota = (OTA::Handle*) multipart->fn_data;
//       if (ota == nullptr)
//         return;

//       // Even if MG_F_OTA_FAILED is set we should let the OTA try to clean up

//       ESP_LOGI(TAG, "Ending OTA...");
//       OTA::end_result_t result = ota->end();

//       // Save callback
//       callbacks.push(result.callback);

//       // Update response if error
//       if (result.status != ESP_OK)
//       {
//         response += "OTA end failed.";
//         c->flags |= MG_F_OTA_FAILED;
//       }

//       // Free the handle object and reference
//       delete ota;
//       multipart->fn_data = nullptr;
//       break;
//     }

//     case MG_EV_HTTP_MULTIPART_REQUEST_END:
//     {
//       // Send the appropriate reply
//       const char* reply = c->flags & MG_F_OTA_FAILED ? response.c_str() : "OTA update successful.";
      
//       mg_send_head(c, c->flags & MG_F_OTA_FAILED ? 500 : 200, strlen(reply), "Content-Type: text/html");
//       mg_printf(c, reply);
//       c->is_draining = true;

//       // Signal OTA is complete
//       c->flags |= MG_F_OTA_COMPLETE;

//       response.clear();
//       break;
//     }

//     case MG_EV_CLOSE:
//     {
//       // Ignore close events that aren't after an OTA
//       if ((c->flags & MG_F_OTA_COMPLETE) != MG_F_OTA_COMPLETE)
//        return;
      
//       // Fire callbacks once OTA connection has closed
//       while (callbacks.empty() == false)
//       {
//         OTA::end_callback_t callback = callbacks.front();
//         if (callback)
//           callback();

//         callbacks.pop();
//       }
    
//      break;
//     }
//   }
// }

/**
  @brief  Main task function of the HTTP server
  
  @param  pvParameters
  @retval none
*/
void HTTP::task(void* pvParameters)
{
  ESP_LOGI(TAG, "Starting HTTP server.");

  client_mutex = xSemaphoreCreateMutex();
  if (client_mutex == nullptr)
  {
    ESP_LOGE(TAG, "Failed to create client mutex.");
    return;
  }

  Warthog warthog;
  warthog.register_default(httpEventHandler);

  // Create and init the event manager
  struct mg_mgr manager;
  mg_mgr_init(&manager);

  // Connect bind to an address and specify the event handler
  struct mg_connection* connection = mg_http_listen(&manager, "http://0.0.0.0:80", Warthog::mongoose_event_handler, &warthog);
  if (connection == NULL)
  {
    ESP_LOGE(TAG, "Failed to bind HTTP port.");
    mg_mgr_free(&manager);
    vTaskDelete(NULL);
    return;
  }

  // Special handler for OTA page
  // mg_register_http_endpoint(connection, "/ota", otaEventHandler, nullptr);

  // Construct the PCM stream object
  StreamConfig pcm("PCM", "Content-Type: audio/L16;rate=48000;channels=2\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0\r\n");

  // Construct the WAV stream object
  StreamConfig wav("WAV", "Content-Type: audio/wav\r\nAccept-Ranges: none\r\nCache-Control: no-cache,no-store,must-revalidate,max-age=0\r\n");
  wav.setup = [](struct mg_connection* c)
  {
    // Construct and send the WAV header
    WAV::Header wav_header(48000);
    mg_send(c, &wav_header, sizeof(wav_header));
  };

  // Add separate end points for raw PCM and WAV stream
  warthog.register_endpoint("/stream.pcm", httpStreamEventHandler, &pcm);
  warthog.register_endpoint("/stream.wav", httpStreamEventHandler, &wav);

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
  if (client_mutex == nullptr || xSemaphoreTake(client_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
  {
    ESP_LOGE(TAG, "Failed to lock client mutex.");
    return;
  }

  for (const auto c : clients)
  {
    // Get queue handle from the connection
    QueueHandle_t queue = (QueueHandle_t) c->fn_data;
    assert(queue != nullptr);

    // Attempt to queue the sample data
    if (xQueueSendToBack(queue, samples.data(), pdMS_TO_TICKS(50)) == pdTRUE)
      continue;
    
    ESP_LOGW(TAG, "Client %p queue overflow.", c);

    // Free up space in the queue
    I2S::sample_buffer_t dummy;
    if (xQueueReceive(queue, dummy.data(), 0) != pdTRUE)
      ESP_LOGE(TAG, "Failed to pop from full queue.");

    // Queue without blocking this time
    if (xQueueSendToBack(queue, samples.data(), 0) != pdTRUE)
      ESP_LOGE(TAG, "Failed to queue samples for %p.", c);
  }

  xSemaphoreGive(client_mutex);
}