#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "rest_methods.h"

#define MAX_HTTP_OUTPUT_BUFFER_POST (1024 * 2)

esp_err_t _http_event_handler_post(esp_http_client_event_t *evt)
{
    static char *TAG = "POST_HANDLER";
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "Connected");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "Header Sent");
        break;
    case HTTP_EVENT_ON_HEADER:
        // This is where the header name and value are received
        ESP_LOGI(TAG, "Received header: %s: %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            if (evt->user_data)
            {
                memcpy(evt->user_data + output_len, evt->data, evt->data_len);
            }
            else
            {
                if (output_buffer == NULL)
                {
                    output_buffer = (char *)malloc(esp_http_client_get_content_length(evt->client));
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
            }
            output_len += evt->data_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        break;
    case HTTP_EVENT_REDIRECT:  // Add the missing case
        // Use esp_http_client_get_status_code to get the status code for the redirect event
        int status_code = esp_http_client_get_status_code(evt->client);
        ESP_LOGI(TAG, "Redirect event received, status code: %d", status_code);
        break;
    }
    return ESP_OK;
}

// Ensure no other blocking operations occur before sending HTTP request
int POST(const char* server_uri, const char* to_send)
{
    const char *TAG = "POST";
    // Make sure that any prior locks or mutexes are released before sending the request
    //printf("Sending POST request to: %s\n", server_uri);
    //printf("post: %s\n", to_send);
    ESP_LOGI(TAG, "Sending POST request to: %s\n", server_uri);
    ESP_LOGI(TAG, "Data to send: %s\n", to_send);

    char local_response_buffer_post[MAX_HTTP_OUTPUT_BUFFER_POST] = {0};

    esp_http_client_handle_t http_client_post;

    esp_http_client_config_t config = {
        .host = "", // Set host dynamically if needed
        .path = "", // Set path dynamically if needed
        .event_handler = _http_event_handler_post,
        .user_data = local_response_buffer_post, // Pass the local buffer for response
    };

    http_client_post = esp_http_client_init(&config);

    // Ensure no mutex is held before performing this
    esp_http_client_set_url(http_client_post, server_uri);
    esp_http_client_set_method(http_client_post, HTTP_METHOD_POST);
    esp_http_client_set_header(http_client_post, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(http_client_post, to_send, strlen(to_send));

    esp_err_t err = esp_http_client_perform(http_client_post);

    int status_code = esp_http_client_get_status_code(http_client_post);
    esp_http_client_cleanup(http_client_post);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "POST Request Successful\n");
        ESP_LOGI(TAG, "HTTP Status Code: %d\n", status_code);
        return status_code;
    }
    else
    {
        ESP_LOGI(TAG, "HTTP POST request failed with error: %s\n", esp_err_to_name(err));
        return status_code; // Return the HTTP status code even on error
    }
}

