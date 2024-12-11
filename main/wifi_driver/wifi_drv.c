#include "wifi_drv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "string.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "../main.h"
#include "data.h"

// Define the maximum size for hostname (12 characters for MAC + 1 for null terminator)
#define MAX_HOSTNAME_LEN 13

#define RETRIES_COUNT 8
#define STATIC_SSID   "sableBusiness"   // Set static SSID here
#define STATIC_PASSWORD "password123"    // Set static password here
static const char *TAG = "WiFi";
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

esp_err_t wifi_connect();

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    static uint8_t retries = RETRIES_COUNT;
    // Declare the hostname variable
    char hostname[MAX_HOSTNAME_LEN];

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Attempting to connect to Wi-Fi...");
            esp_wifi_connect();  // Start the connection process
        }
        else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            // Get the MAC address
            uint8_t mac[6];
            esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);

            // Convert the MAC address to a string without colons
    snprintf(main_struct.hostname, MAX_HOSTNAME_LEN, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

            // Log the assigned hostname
            ESP_LOGI(TAG, "Assigned hostname: %s", main_struct.hostname);
            ESP_LOGI(TAG, "Wi-Fi Connected");
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (retries > 0) {
                ESP_LOGI(TAG, "Wi-Fi disconnected, retrying... (%d retries left)", retries);
                esp_wifi_connect();
                retries--;
            } else {
                ESP_LOGE(TAG, "Wi-Fi connection failed after maximum retries.");
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                main_struct.isProvisioned = false;
                // Optionally restart the Wi-Fi or device to retry the configuration
                // esp_restart();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        main_struct.isProvisioned = true;
        monitor();  // Call your monitoring function after a successful connection
    }
}

void wifi_init(void)
{
    // Create event group
    wifi_event_group = xEventGroupCreate();

    // Initialize Wi-Fi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_create_default_wifi_sta();

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Try to connect to Wi-Fi
    wifi_connect();

    ESP_ERROR_CHECK(esp_wifi_start());
}

esp_err_t wifi_connect()
{
    wifi_config_t wifi_config = {0};

    // Check if STATIC_SSID and STATIC_PASSWORD are set
    #ifdef STATIC_SSID
        // Use the static SSID for testing
        strncpy((char *)wifi_config.sta.ssid, STATIC_SSID, sizeof(wifi_config.sta.ssid) - 1);
        ESP_LOGI(TAG, "Using Static SSID: %s", STATIC_SSID);
    #else
        // If STATIC_SSID is not set, handle accordingly (e.g., use default, or NVS)
        ESP_LOGI(TAG, "STATIC_SSID is not set, falling back to default.");
    #endif

    #ifdef STATIC_PASSWORD
        // Use the static password for testing
        strncpy((char *)wifi_config.sta.password, STATIC_PASSWORD, sizeof(wifi_config.sta.password) - 1);
        ESP_LOGI(TAG, "Using Static Password: %s", STATIC_PASSWORD);
    #else
        // Set SSID and password from main_struct (ensure they are valid strings)
        strncpy((char *)wifi_config.sta.ssid, main_struct.ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, main_struct.password, sizeof(wifi_config.sta.password) - 1);
    #endif

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", wifi_config.sta.ssid);

    // Set Wi-Fi config and start connection
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Start the connection attempt
    return esp_wifi_connect();
}
