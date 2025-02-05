#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include "main.h"
#include "wifi_drv.h"
#include "nvs_drv.h"
#include "driver/adc.h"
#include "data.h"
#include "rest_methods.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h" 
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"  // Include the correct header for esp_read_mac

#define JSON_CONFIG_UUID 0xFEFA  // UUID for JSON data

#define SERVICE_CHR_UUID 0xFEF3
/*#define SSID_CHR_UUID 0xFEF4
#define PASS_CHR_UUID 0xFEF5
#define NAME_CHR_UUID 0xFEF6  // Example UUID for sensorName
#define LOCATION_CHR_UUID 0xFEF7  // Example UUID for sensorLocation
#define API_TOKEN_UUID 0xFEF8*/
#define HOSTNAME_UUID 0xFEF9
//#define PROV_STATUS_UUID 0xDEAD
#define MANUFACTURER_NAME "Rodland Farms"
// Define GPIO pin for the button
#define BUTTON_GPIO 6


char *TAG = "-";

static bool ssid_pswd_flag[2] = {
    false,
};
uint8_t ble_addr_type;
uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t prov_status_attr_handle = 0;
// Global variable for characteristic handle
uint16_t hostname_attr_handle;

main_struct_t main_struct = {.credentials_recv = 0, .isProvisioned = false};

// Notification function for PROV_STATUS_UUID
void notify_prov_status(uint8_t status_data)
{
    char *TAG = "NOTIFY";
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&status_data, sizeof(status_data));
        if (!om)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for notification");
            return;
        }

        int rc = ble_gattc_notify_custom(g_conn_handle, prov_status_attr_handle, om);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "Error sending notification; rc=%d", rc);
        }
        else
        {
            ESP_LOGI(TAG, "Notification sent: %d", status_data);
        }
    }
    else
    {
        ESP_LOGW(TAG, "No connected client to notify");
    }
}
void get_wifi_mac_address()
{
    #define MAX_HOSTNAME_LEN 32  // Set this to the maximum hostname length allowed
    uint8_t mac[6]; // Array to store the MAC address
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read the MAC address for the Wi-Fi station interface
    // set hostname to the MAC address to a string without colons

    if (err == ESP_OK) {
        snprintf(main_struct.hostname, MAX_HOSTNAME_LEN, "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        /*ESP_LOGI("MAC", "Wi-Fi MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);*/
        ESP_LOGI("HOSTNAME", "hostname: %02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGE("MAC", "Failed to read Wi-Fi MAC address: %s", esp_err_to_name(err));
    }
}
// Write data to ESP32 defined as server
static int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char *TAG = "WRITE";
    size_t len;
    ESP_LOGI(TAG, "UUID: 0x%04x", ble_uuid_u16(ctxt->chr->uuid));


    switch (ble_uuid_u16(ctxt->chr->uuid))
    {

    case JSON_CONFIG_UUID:
        // Convert received data to a null-terminated string
        char json_str[256] = {0}; // Ensure enough space
        strncpy(json_str, (char *)ctxt->om->om_data, ctxt->om->om_len);
        json_str[ctxt->om->om_len] = '\0';

        ESP_LOGI(TAG, "Received JSON: %s", json_str);

        // Parse JSON
        cJSON *root = cJSON_Parse(json_str);
        if (!root) {
            ESP_LOGE(TAG, "JSON Parsing Failed");
            return BLE_ATT_ERR_UNLIKELY;
        }

        // Extract values from JSON
        cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
        cJSON *password = cJSON_GetObjectItem(root, "password");
        cJSON *name = cJSON_GetObjectItem(root, "sensor_name");
        cJSON *location = cJSON_GetObjectItem(root, "sensor_location");
        cJSON *api_token = cJSON_GetObjectItem(root, "api_token");

        if (ssid && password && name && location && api_token) {
            strncpy(main_struct.ssid, ssid->valuestring, sizeof(main_struct.ssid));
            strncpy(main_struct.password, password->valuestring, sizeof(main_struct.password));
            strncpy(main_struct.name, name->valuestring, sizeof(main_struct.name));
            strncpy(main_struct.location, location->valuestring, sizeof(main_struct.location));
            strncpy(main_struct.apiToken, api_token->valuestring, sizeof(main_struct.apiToken));

            ESP_LOGI(TAG, "Parsed Data: SSID=%s, Name=%s, Location=%s", main_struct.ssid, main_struct.name, main_struct.location);

            // Save to NVS
            save_to_nvs(main_struct.ssid, main_struct.password, main_struct.name, main_struct.location, main_struct.apiToken, true);
        }

        cJSON_Delete(root); // Free JSON object
        break;

    /*case SSID_CHR_UUID:
        ssid_pswd_flag[0] = true;
        len = (ctxt->om->om_len < sizeof(main_struct.ssid) - 1) ? ctxt->om->om_len : sizeof(main_struct.ssid) - 1;
        strncpy(main_struct.ssid, (char *)ctxt->om->om_data, len);
        main_struct.ssid[len] = '\0';
        ESP_LOGI(TAG, "Received SSID: %s", main_struct.ssid);
        break;

    case PASS_CHR_UUID:
        ssid_pswd_flag[1] = true;
        len = (ctxt->om->om_len < sizeof(main_struct.password) - 1) ? ctxt->om->om_len : sizeof(main_struct.password) - 1;
        strncpy(main_struct.password, (char *)ctxt->om->om_data, len);
        main_struct.password[len] = '\0';
        ESP_LOGI(TAG, "Received Password: %s", main_struct.password);
        break;

    case NAME_CHR_UUID:
        ssid_pswd_flag[2] = true;
        len = (ctxt->om->om_len < sizeof(main_struct.name) - 1) ? ctxt->om->om_len : sizeof(main_struct.name) - 1;
        strncpy(main_struct.name, (char *)ctxt->om->om_data, len);
        main_struct.name[len] = '\0';
        ESP_LOGI(TAG, "Received Device Name: %s", main_struct.name);
        break;

    case LOCATION_CHR_UUID:
        ssid_pswd_flag[3] = true;
        len = (ctxt->om->om_len < sizeof(main_struct.location) - 1) ? ctxt->om->om_len : sizeof(main_struct.location) - 1;
        strncpy(main_struct.location, (char *)ctxt->om->om_data, len);
        main_struct.location[len] = '\0';
        ESP_LOGI(TAG, "Received Device Location: %s", main_struct.location);
        break;

    case API_TOKEN_UUID:
        ESP_LOGI(TAG, "API_TOKEN_UUID");

        ssid_pswd_flag[4] = true;
        strncpy(main_struct.apiToken, (char *)ctxt->om->om_data, ctxt->om->om_len);
        main_struct.apiToken[ctxt->om->om_len] = '\0';
        ESP_LOGI(TAG, "Received Device API Token: %s", main_struct.apiToken);
        break;

    default:
        ESP_LOGW(TAG, "Unknown characteristic written.");
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ssid_pswd_flag[0] && ssid_pswd_flag[1]) // ssid and password received
    {

        main_struct.credentials_recv = true;
        save_to_nvs(main_struct.ssid, main_struct.password, main_struct.name, main_struct.location, main_struct.apiToken, main_struct.credentials_recv);
        }

    return 0;
    }


// Read data from ESP32 defined as server
static int device_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Check the attribute handle and decide what to send
    if (attr_handle == prov_status_attr_handle) {
        os_mbuf_append(ctxt->om, main_struct.isProvisioned, sizeof(main_struct.isProvisioned));
        ESP_LOGI("BLE", "BLE Read: Provisioning Status - %u", main_struct.isProvisioned);
    } 
    if (attr_handle == hostname_attr_handle) {
        os_mbuf_append(ctxt->om, main_struct.hostname, strlen(main_struct.hostname));
        ESP_LOGI("BLE", "BLE Read: Hostname - %s", main_struct.hostname);
    } else {
        ESP_LOGE("BLE", "BLE Read: Unknown attribute handle");
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0; // Success
}



// Array of pointers to other service definitions
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,  // Primary service type
        .uuid = BLE_UUID16_DECLARE(SERVICE_CHR_UUID), // Service UUID (Custom UUID for device type)
        .characteristics = (struct ble_gatt_chr_def[]){
            /*{
                .uuid = BLE_UUID16_DECLARE(SSID_CHR_UUID), // SSID characteristic
                .flags = BLE_GATT_CHR_F_WRITE, // Write access only
                .access_cb = device_write      // Write callback
            },
            {
                .uuid = BLE_UUID16_DECLARE(PASS_CHR_UUID), // Password characteristic
                .flags = BLE_GATT_CHR_F_WRITE, // Write access only
                .access_cb = device_write      // Write callback
            },
            {
                .uuid = BLE_UUID16_DECLARE(NAME_CHR_UUID), // Sensor Name characteristic
                .flags = BLE_GATT_CHR_F_WRITE, // Write access only
                .access_cb = device_write      // Write callback
            },
            {
                .uuid = BLE_UUID16_DECLARE(LOCATION_CHR_UUID), // Sensor Location characteristic
                .flags = BLE_GATT_CHR_F_WRITE, // Write access only
                .access_cb = device_write      // Write callback
            },
            {
                .uuid = BLE_UUID16_DECLARE(API_TOKEN_UUID), // Define UUID for reading
                .flags = BLE_GATT_CHR_F_WRITE, // Write access only
                .access_cb = device_write,      // Write callback
            },*/
            {
                .uuid = BLE_UUID16_DECLARE(HOSTNAME_UUID),
                .access_cb = device_read,
                .val_handle = &hostname_attr_handle, // Assign handle
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(JSON_CONFIG_UUID),
                .flags = BLE_GATT_CHR_F_WRITE,
                .access_cb = device_write,
            },
            {0} // Terminating the characteristics array
        }
    },
    {0} // Terminating the services array
};

// Function to send a notification
static void send_hostname()
{
    struct os_mbuf *om;

    // Allocate memory for the message
    om = ble_hs_mbuf_from_flat(main_struct.hostname, strlen(main_struct.hostname));
    if (!om)
    {
        ESP_LOGE("BLE", "Failed to allocate memory for notification");
        return;
    }

    // Ensure connection handle and attribute handle are valid before sending the notification
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || hostname_attr_handle == 0)
    {
        ESP_LOGE("BLE", "Invalid connection or attribute handle.");
        os_mbuf_free_chain(om); // Free allocated memory
        return;
    }

    // Send notification
    int rc = ble_gattc_notify_custom(g_conn_handle, hostname_attr_handle, om);
    if (rc != 0)
    {
        ESP_LOGE("BLE", "Failed to send notification, error: %d", rc);
    }
    else
    {
        ESP_LOGI("BLE", "Notification sent: %s", main_struct.hostname);
    }

    // Free the memory after sending
    os_mbuf_free_chain(om);
}


// BLE event handling
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    // Advertise if connected
    case BLE_GAP_EVENT_CONNECT:

        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI("BLE", "Connection handle: %d", g_conn_handle);
            get_wifi_mac_address();
            send_hostname();
        } else {
            ble_advert();
        }

        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "CONNECTED!" : "DISCONNECTED!");
        ESP_LOGI("GAP", "Rebooting...");
        vTaskDelay(100);
        esp_restart();
        
        break;
    // Advertise again after completion of the event/advertisement
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        ble_advert();

        break;
    default:
        break;
    }
    return 0;
}

#define LED_GPIO 2

void blink_led(void *arg) {
    esp_rom_gpio_pad_select_gpio(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);  // Initially turn off the LED
    while (!main_struct.credentials_recv) {
        gpio_set_level(LED_GPIO, 1); // Turn LED on
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for 500ms
        gpio_set_level(LED_GPIO, 0); // Turn LED off
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait for 500ms
    }

    // Once the configuration is successful, turn off the LED
    gpio_set_level(LED_GPIO, 0);
    vTaskDelete(NULL);
}

void ble_app_advertise(void) {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    const char *device_name;
    //const char *company_identifier= "Rodland Farms";  
    device_name = ble_svc_gap_device_name(); // Read the BLE device name; 

    // Prepare advertising data
    uint8_t advertising_data[31];  // Maximum size for BLE advertising data is 31 bytes
    uint8_t len = 0;

    // Set the flags (LE General Discoverable | LE BR/EDR Not Supported)
    advertising_data[len++] = 2;                 // Length of flags
    advertising_data[len++] = 0x01;              // Type: Flags
    advertising_data[len++] = 0x02 | 0x06;       // Flags: LE General Discoverable | LE BR/EDR Not Supported

    // Add device name to the advertising data
    uint8_t name_len = strlen(device_name) + 1;  // +1 for the 0x09 type byte
    advertising_data[len++] = name_len;          // Length of the name
    advertising_data[len++] = 0x09;              // Type: Complete Local Name
    memcpy(&advertising_data[len], device_name, name_len);
    len += name_len;

    // Add 16-bit UUID
    advertising_data[len++] = 3;  // Length of UUID field (1 byte for type + 2 bytes for UUID)
    advertising_data[len++] = 0x03; // Type: 16-bit UUIDs

    // Add SERVICE_CHR_UUID (little-endian)
    advertising_data[len++] = (uint8_t)(SERVICE_CHR_UUID & 0xFF);
    advertising_data[len++] = (uint8_t)((SERVICE_CHR_UUID >> 8) & 0xFF);


    // Set the advertising data
    int rc = ble_gap_adv_set_data(advertising_data, len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising data, error code %d", rc);
        return;
    }

    // Define advertisement parameters
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // Discoverable

    // Start advertising
    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising, error code %d", rc);
    }
    // Start the task to blink the LED while waiting for configuration
    xTaskCreate(blink_led, "Blink LED", 2048, NULL, 5, NULL);
}

static void nimble_host_task(void *param)
{
    char *TAG = "NimBLE";
    // Task entry log 
    ESP_LOGI(TAG, "NimBLE host task has been started!");

    // This function won't return until nimble_port_stop() is executed
    nimble_port_run();

    // Clean up at exit 
    vTaskDelete(NULL);
}

void disable_ble(void)
{
    char *TAG = "DISABLE_BLE";
    // Stop BLE advertising if running
    int rc = ble_gap_adv_stop();
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE advertising stopped.");
    } else {
        ESP_LOGE(TAG, "Failed to stop BLE advertising: %d", rc);
    }

    // Optionally, stop the NimBLE stack completely (if desired)
    nimble_port_stop(); // Uncomment if you want to disable all BLE functionality
}

void check_credentials(void *arg)
{
    char *TAG = "CHECK_CREDS";
    while (1)
    {
        //if (main_struct.credentials_recv)
        //{
            ESP_LOGI(TAG, "Checking WiFi Credentials");
            wifi_init();
            //disable_ble();
            vTaskDelete(NULL);
        //}

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void nvs_init(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

// Function to erase NVS data
void erase_nvs_data() {
    esp_err_t err;
    nvs_handle my_handle;

    // Open NVS for writing
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error opening NVS for erase: %s", esp_err_to_name(err));
        return;
    }

    // Erase all data stored in NVS
    err = nvs_erase_all(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error erasing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI("NVS", "NVS data erased successfully.");
    }

    // Commit changes and close NVS
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error committing NVS: %s", esp_err_to_name(err));
    }
    nvs_close(my_handle);
}

// Function to configure GPIO for the button
void configure_button_gpio() {
    esp_rom_gpio_pad_select_gpio(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);  // Pull-up to avoid floating state
}

// Function to monitor the button press and trigger NVS erase
#define SHORT_PRESS_THRESHOLD 1000  // Time in milliseconds for a short press (e.g., 1 second)
#define LONG_PRESS_THRESHOLD 3000   // Time in milliseconds for a long press (e.g., 3 seconds)

void monitor_button_press(void *pvParameter) {
    static bool button_pressed = false;  // Tracks whether the button is pressed
    static TickType_t press_start_time = 0;  // Holds the time when the button was first pressed

    while (1) {
        int button_state = gpio_get_level(BUTTON_GPIO);  // Read the current state of the button

        if (button_state == 0 && !button_pressed) {  // Button pressed (active low) and not already processed
            button_pressed = true;  // Mark button as pressed
            press_start_time = xTaskGetTickCount();  // Store the time when the button was pressed
            ESP_LOGI("BUTTON", "Button press detected...");
        }

        if (button_state == 1 && button_pressed) {  // Button released (active low)
            button_pressed = false;  // Reset button state

            TickType_t press_duration = xTaskGetTickCount() - press_start_time;  // Calculate press duration in ticks
            uint32_t press_duration_ms = press_duration * portTICK_PERIOD_MS;  // Convert ticks to milliseconds

            // Check if the press duration was a short or long press
            if (press_duration_ms < SHORT_PRESS_THRESHOLD) {
                ESP_LOGI("BUTTON", "Short press detected...");
                //erase_nvs_data();  // Perform short press action (e.g., erase NVS)
            } else if (press_duration_ms >= LONG_PRESS_THRESHOLD) {
                ESP_LOGI("BUTTON", "Long press detected. Erasing NVS and Rebooting...");
                erase_nvs_data();
                vTaskDelay(100 / portTICK_PERIOD_MS);  // Allow time for logging
                esp_restart();  // Perform long press action (e.g., reboot device)
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);  // Short polling interval to keep the system responsive
    }
}


void notify_status(void *arg)
{
    char *TAG = "NOTIFY_STATUS";
    // The BLE stack is now synchronized and ready to run BLE operations
    ESP_LOGI(TAG, "BLE sync completed.");

    while (1)
    {
        if (main_struct.isProvisioned)
        {
            notify_prov_status(main_struct.isProvisioned); // Send notification
            main_struct.isProvisioned = 0;

            vTaskDelete(NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Check every second
    }
}
// Helper function to convert the MAC address to a string
void mac_to_string(uint8_t *mac, char *mac_str) {
    // Format only the last 4 bytes of the MAC address into the string
    sprintf(mac_str, "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
}


// BLE sync callback function
void ble_app_on_sync(void)
{
    char *TAG = "BLE_SYNC";
    // The BLE stack is now synchronized and ready to run BLE operations
    ESP_LOGI(TAG, "BLE sync completed.");

    // Retrieve the MAC address
    uint8_t mac[6];   // Array to hold the MAC address (6 bytes)
    char device_name[32];  // Buffer to hold the MAC address as a string

    // Automatically infer the address type and MAC address
    ble_hs_id_infer_auto(0, &ble_addr_type);  // Infers the address type
    ble_hs_id_infer_auto(0, mac);  // Retrieves the MAC address

    // Convert only the last 4 bytes of the MAC address to string (without colons)
    mac_to_string(mac + 2, device_name);  // Only use last 4 bytes of MAC address

    // Create final device name with "PlantPulse-" prefix and the last 4 bytes of the MAC address
    char final_device_name[50];  // Ensure enough space for "PlantPulse-" + MAC address
    snprintf(final_device_name, sizeof(final_device_name), "Plant Pulse %s", device_name);

    // Set the BLE device name
    ble_svc_gap_device_name_set(final_device_name);  // Set the device name
    
    ESP_LOGI(TAG, "Device BLE name set to: %s", final_device_name);
    
    // Initialize services
    //ble_svc_gap_init();                        // Initialize NimBLE GAP service
    //ble_svc_gatt_init();                       // Initialize NimBLE GATT service
    //ble_gatts_count_cfg(gatt_svcs);            // Initialize NimBLE config GATT services
    //ble_gatts_add_svcs(gatt_svcs);             // Add GATT services
    ble_att_set_preferred_mtu(256); // Increase BLE MTU to store apitoken
    ble_app_advertise();
}

void ble_advert(void){
    char *TAG = "BLE_ADVERT";
    ESP_LOGI(TAG, "Starting BLE advertising for provisioning...");
    // Initialize NimBLE host stack
    nimble_port_init();                        // 3 - Initialize the host stack
    ble_svc_gap_init();                        // 4 - Initialize NimBLE configuration - gap service
    ble_svc_gatt_init();                       // 4 - Initialize NimBLE configuration - gatt service
    ble_gatts_count_cfg(gatt_svcs);            // 4 - Initialize NimBLE configuration - config gatt services
    ble_gatts_add_svcs(gatt_svcs);             // 4 - Initialize NimBLE configuration - queues gatt services.
    ble_hs_cfg.sync_cb = ble_app_on_sync;      // 5 - Initialize application


    // Start NimBLE host task thread and return 
    xTaskCreate(nimble_host_task, "PlantPulse", 8 * 1024, NULL, 5, NULL);
    // Task entry log 
    ESP_LOGI(TAG, "NimBLE host task has been started!");

}

// Function to enter deep sleep based on the selected duration
void enter_deep_sleep(SleepDuration duration)
{
    //#define BUTTON_GPIO 6
    static const char *TAG = "SLEEP";
    uint64_t sleep_duration_us = (uint64_t)duration * (uint64_t)1000000; // Convert seconds to microseconds

    ESP_LOGI(TAG, "Entering deep sleep mode for %d seconds...", duration);

    // Configure the RTC timer to wake up after the specified sleep duration
    esp_sleep_enable_timer_wakeup(sleep_duration_us);

    // Configure the button GPIO to wake up the device on a button press (active low)
    esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);  // 0 for active low, 1 for active high


    // Enter deep sleep
    esp_deep_sleep_start();
}

// Main application entry point
void app_main() {
    char *TAG = "MAIN";
    // Configure GPIO for button
    configure_button_gpio();
    // Configure ADC1 channel 5 (GPIO5) for 12-bit width and 11dB attenuation
    adc1_config_width(ADC_WIDTH_BIT_12);  // Set ADC width to 12 bits (0-4095 range)
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);  // Set ADC attenuation to 11dB (0-3.6V range)
    // Start a task to monitor the button press
    xTaskCreate(monitor_button_press, "monitor_button_press", 2 * 1024, NULL, 5, NULL);

    nvs_init();
    read_from_nvs(main_struct.ssid, main_struct.password, main_struct.name, main_struct.location, main_struct.apiToken, &main_struct.credentials_recv);

    ESP_LOGI("NVS", "SSID: %s", main_struct.ssid);
    ESP_LOGI("NVS", "Password: %s", main_struct.password);
    ESP_LOGI("NVS", "Name: %s", main_struct.name);
    ESP_LOGI("NVS", "Location: %s", main_struct.location);
    ESP_LOGI("NVS", "API key: %s", main_struct.apiToken);
    ESP_LOGI("NVS", "Credentials Received: %d", main_struct.credentials_recv);

    // Only initialize BLE if credentials are NOT set
    if (!main_struct.credentials_recv) {
        ble_advert();
    } else {
    ESP_LOGI(TAG, "Wi-Fi credentials already set. Skipping BLE provisioning.");
    xTaskCreate(check_credentials, "check_credentials", 4 * 1024, NULL, 5, NULL);

    //xTaskCreate(notify_status, "notify_status", 2 * 1024, NULL, 5, NULL);
}

}