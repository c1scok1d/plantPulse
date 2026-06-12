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
#include "esp_crt_bundle.h"   // root-CA bundle, so https:// uploads validate (same path OTA uses)
#include "rest_methods.h"

// The telemetry upload only needs the HTTP status code, not the response body. The
// previous handler memcpy'd the entire response into a fixed 2 KB stack buffer with
// NO bounds check, which overflowed (and burned ~5 s of wake time) whenever the
// server returned a large body (e.g. a Laravel HTML error page). We deliberately
// ignore the response body here.
esp_err_t _http_event_handler_post(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // Intentionally drop the response body — we don't read it.
        break;
    default:
        break;
    }
    return ESP_OK;
}

// Ensure no other blocking operations occur before sending HTTP request
int POST(const char* server_uri, const char* to_send)
{
    const char *TAG = "POST";
    ESP_LOGI(TAG, "Sending POST request to: %s", server_uri);

    esp_http_client_handle_t http_client_post;

    esp_http_client_config_t config = {
        .url = server_uri,                           // REQUIRED at init time: without a URL
                                                     // (or host+path) esp_http_client_init()
                                                     // returns NULL and the set_* calls below
                                                     // dereference it -> StoreProhibited panic.
        .event_handler = _http_event_handler_post,
        .crt_bundle_attach = esp_crt_bundle_attach,  // validate TLS for https:// uploads
        .timeout_ms = 8000,
    };

    http_client_post = esp_http_client_init(&config);
    if (http_client_post == NULL) {
        // Don't crash-loop the whole device on a bad/empty URL — skip this upload.
        ESP_LOGE(TAG, "esp_http_client_init failed (uri='%s')", server_uri ? server_uri : "(null)");
        return -1;
    }

    // Ensure no mutex is held before performing this
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

