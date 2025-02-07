#include "wifi_drv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "string.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "main.h"
#include "data.h"
#include <sys/time.h>  // For gettimeofday()


// Define the maximum size for hostname (12 characters for MAC + 1 for null terminator)
#define MAX_HOSTNAME_LEN 13

#define RETRIES_COUNT 8
//#define STATIC_SSID   "thespot"   // Set static SSID here
//#define STATIC_PASSWORD "Password123"    // Set static password here
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
    //char hostname[MAX_HOSTNAME_LEN];

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Attempting to connect to Wi-Fi...");
            esp_wifi_connect();  // Start the connection process
        }
        else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            // Get the MAC address
            uint8_t mac[6];
            esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);

            // set hostname to the MAC address to a string without colons
            snprintf(main_struct.hostname, MAX_HOSTNAME_LEN, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

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
                esp_wifi_stop();  // Stop the Wi-Fi driver
                ESP_LOGI(TAG, "Wi-Fi disabled.");
                // Delay to allow time for error logging and BLE advertisement setup
                //vTaskDelay(1000);

                // Trigger BLE advertising for provisioning
                ble_advert();

                // Optionally restart the Wi-Fi or device to retry the configuration
                // esp_restart();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        main_struct.isProvisioned = true;
        // Initialize SNTP to set time
        initialize_sntp();

        // Wait for time sync to complete
        wait_for_time_sync();

        ESP_LOGI("NTP", "Time successfully synchronized!");
        struct timeval tv;
        gettimeofday(&tv, NULL);  // Get current time

        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);  // Convert to local time

        char time_str[64];
        strftime(time_str, sizeof(time_str), "%c", &timeinfo);  // Format time as a readable string

        ESP_LOGI("NTP", "Current time: %s", time_str);  // Log current time

         // Check for firmwareupdate
        xTaskCreate(&check_update, "check_update", 8192, NULL, 5, NULL);
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


    // If STATIC_PASSWORD is defined, use that
    #if STATIC_PASSWORD
        strncpy((char *)wifi_config.sta.password, STATIC_PASSWORD, sizeof(wifi_config.sta.password) - 1);
        ESP_LOGI(TAG, "Using Static Password: %s", STATIC_PASSWORD);
    #else
        ESP_LOGI(TAG, "Static PASSWORD not set. Checking NVS settings.");
        // If STATIC_PASSWORD is not defined, fall back to main_struct.password
        if (strlen(main_struct.password) == 0) {
            // If password is not set, start BLE advertising for provisioning
            ESP_LOGI(TAG, "NVS not set, starting BLE advertising for provisioning...");
            main_struct.isProvisioned = false;
            esp_wifi_stop();  // Stop the Wi-Fi driver
            ESP_LOGI(TAG, "Wi-Fi disabled.");
            ble_advert();  // Start BLE advertising for provisioning
            return ESP_ERR_WIFI_NOT_CONNECT;  // Return error code to indicate failure to connect
        }
        strncpy((char *)wifi_config.sta.password, main_struct.password, sizeof(wifi_config.sta.password) - 1);
        strncpy((char *)wifi_config.sta.ssid, main_struct.ssid, sizeof(wifi_config.sta.ssid) - 1);
        ESP_LOGI(TAG, "Using Password from main_struct.");
    #endif

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", wifi_config.sta.ssid);

    // Set the Wi-Fi configuration and start the connection attempt
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Start the connection attempt
    return ESP_OK;
}

