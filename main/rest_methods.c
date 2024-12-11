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
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        break;
    case HTTP_EVENT_ON_CONNECTED:
        break;
    case HTTP_EVENT_HEADER_SENT:
        break;
    case HTTP_EVENT_ON_HEADER:
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
    }
    return ESP_OK;
}

int POST(const char* server_uri, const char* to_send)
{
    printf("Sending POST request to: %s\n", server_uri);
    printf("Data: %s\n", to_send);

    char local_response_buffer_post[MAX_HTTP_OUTPUT_BUFFER_POST] = {0};

    esp_http_client_handle_t http_client_post;

    esp_http_client_config_t config = {
        .host = "", // Set host dynamically if needed
        .path = "", // Set path dynamically if needed
        .event_handler = _http_event_handler_post,
        .user_data = local_response_buffer_post, // Pass the local buffer for response
    };

    http_client_post = esp_http_client_init(&config);

    esp_http_client_set_url(http_client_post, server_uri);
    esp_http_client_set_method(http_client_post, HTTP_METHOD_POST);
    esp_http_client_set_header(http_client_post, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(http_client_post, to_send, strlen(to_send));

    esp_err_t err = esp_http_client_perform(http_client_post);

    int status_code = esp_http_client_get_status_code(http_client_post);
    esp_http_client_cleanup(http_client_post);

    if (err == ESP_OK)
    {
        printf("POST Request Successful\n");
        printf("HTTP Status Code: %d\n", status_code);
        return status_code;
    }
    else
    {
        printf("HTTP POST request failed with error: %s\n", esp_err_to_name(err));
        return status_code; // Return the HTTP status code even on error
    }
}
