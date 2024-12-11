#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "http.h"

#define POST_URL "http://athome.rodlandfarms.com/api/esp/data"
#define TAG "HTTP_POST_BUFFER"
#define RETRIES 3

// Function to send a binary buffer via HTTP POST
esp_err_t http_post(char *buffer, size_t buffer_len)
{
    uint8_t retries = RETRIES;
    esp_err_t err = ESP_FAIL;

    esp_http_client_config_t config = {
        .url = POST_URL, // Your endpoint URL
        .timeout_ms = 10000};

    // Initialize HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, (const char *)buffer, buffer_len);

    while (retries > 0)
    {
        err = esp_http_client_perform(client);
        // Check the response
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "HTTP POST Request succeeded. Response code: %d", esp_http_client_get_status_code(client));

            char response[256];
            int len = esp_http_client_read_response(client, response, sizeof(response) - 1);
            if (len >= 0)
            {
                response[len] = '\0';
                ESP_LOGI(TAG, "Response: %s", response);
            }

            break;
        }
        else
        {
            ESP_LOGE(TAG, "HTTP POST Request failed. Error: %s", esp_err_to_name(err));
            retries--; // Decrement retries
            if (retries > 0)
            {
                ESP_LOGI(TAG, "Retrying... (%d attempts left)", retries);
            }
        }
    }

    // Cleanup
    esp_http_client_cleanup(client);

    return err;
}