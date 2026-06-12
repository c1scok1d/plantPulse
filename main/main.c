#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "driver/rtc_io.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"   // key/bond store for BLE pairing
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
#include "cert.h"
#include "esp_sntp.h"
#include "esp_wifi.h"


#define JSON_CONFIG_UUID 0xFEFA  // UUID for JSON data
#define SERVICE_CHR_UUID 0xFEF3
#define HOSTNAME_UUID 0xFEF9
#define MANUFACTURER_NAME "Rodland Farms"
// Define GPIO pin for the button
#define BUTTON_GPIO 3   // V5: SW1 is on IO3 (RTC-capable, used for ext0 wake). Was GPIO6 (V4/legacy).
#define OTA_URL "https://athome.rodlandfarms.com/firmware.bin"
#define SERVER_URL "https://athome.rodlandfarms.com/firmware.json"
#define CURRENT_VERSION "1.0.0"  // Define the current version number of your firmware

char *TAG = "-";

uint8_t ble_addr_type;
uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t prov_status_attr_handle = 0;
// Global variable for characteristic handle
uint16_t hostname_attr_handle;

main_struct_t main_struct = {.credentials_recv = 0, .isProvisioned = false};

// NTP Sync Callback
static bool time_is_set = false;

void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI("NTP", "Time synchronized with NTP server");
    time_is_set = true;
}

// Initialize SNTP and sync time
void initialize_sntp() {
    ESP_LOGI("NTP", "Initializing SNTP...");
    esp_sntp_init();
    const char *ntp_server = "pool.ntp.org";  // NTP server
    esp_sntp_setservername(0, ntp_server);    // Set NTP server
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_restart();
}

// Wait for time synchronization
void wait_for_time_sync() {
    while (!time_is_set) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait for 1 second
    }
}

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
#define MAX_JSON_SIZE 4096  // Adjust buffer size as needed

// Global buffer to store incoming JSON
static char json_buffer[MAX_JSON_SIZE] = {0};
static int json_index = 0;  // Track buffer position

static void send_hostname(void);  // forward decl: notify hostname on 0xFEF9
void ble_app_advertise(void);     // forward decl: (re)start BLE advertising
extern void ble_store_config_init(void);  // NimBLE key/bond store init (ESP-IDF provides it)


// Try to parse the accumulated provisioning JSON.
// Returns true once a COMPLETE JSON object has been received (so the caller resets the
// reassembly buffer), false if it isn't complete yet (keep accumulating BLE chunks).
// Using cJSON_Parse success as the "complete" signal — instead of the old "buffer ends
// in '}'" heuristic — is string-aware, so a '}' inside a value (e.g. a WiFi password)
// landing on a chunk boundary can no longer trigger an early/failed parse.
static bool parse_json(void) {
    cJSON *root = cJSON_Parse(json_buffer);
    if (!root) {
        return false;  // not a complete JSON object yet; wait for more chunks
    }

    // Extract values from JSON
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    cJSON *name = cJSON_GetObjectItem(root, "sensor_name");
    cJSON *location = cJSON_GetObjectItem(root, "sensor_location");
    cJSON *api_token = cJSON_GetObjectItem(root, "api_token");

    if (cJSON_IsString(ssid) && cJSON_IsString(password) && cJSON_IsString(name) &&
        cJSON_IsString(location) && cJSON_IsString(api_token)) {
        strncpy(main_struct.ssid, ssid->valuestring, sizeof(main_struct.ssid) - 1);
        strncpy(main_struct.password, password->valuestring, sizeof(main_struct.password) - 1);
        strncpy(main_struct.name, name->valuestring, sizeof(main_struct.name) - 1);
        strncpy(main_struct.location, location->valuestring, sizeof(main_struct.location) - 1);
        strncpy(main_struct.apiToken, api_token->valuestring, sizeof(main_struct.apiToken) - 1);

        // Optional: per-device deep-sleep interval (seconds). Backward-compatible —
        // absent means keep the existing/default value.
        cJSON *sleep = cJSON_GetObjectItem(root, "sleep_seconds");
        if (cJSON_IsNumber(sleep) && sleep->valueint > 0) {
            nvs_set_sleep_seconds((uint32_t)sleep->valueint);
        }

        ESP_LOGI(TAG, "Parsed Data: SSID=%s, Name=%s, Location=%s", main_struct.ssid, main_struct.name, main_struct.location);

        // Save to NVS
        save_to_nvs(main_struct.ssid, main_struct.password, main_struct.name, main_struct.location, main_struct.apiToken, true);
    } else {
        ESP_LOGE(TAG, "JSON complete but required fields missing/invalid!");
    }

    cJSON_Delete(root);  // Free JSON object
    return true;  // a complete object was consumed -> caller resets the buffer
}


static int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char *TAG="BLE_WRITE";
    struct os_mbuf *om = ctxt->om;

    if (!om || om->om_len == 0) {  
        ESP_LOGE(TAG, "BLE Write received NULL or empty buffer");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    size_t len = om->om_len;

    // Prevent buffer overflow
    if (json_index + len >= MAX_JSON_SIZE) {
        ESP_LOGE(TAG, "Buffer overflow! JSON too large.");
        json_index = 0;  // Reset buffer
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    // Append received chunk to buffer
    strncat(json_buffer, (char *)om->om_data, len);
    json_index += len;
    json_buffer[json_index] = '\0';  // Ensure null termination

    ESP_LOGI(TAG, "Received JSON chunk (%d bytes)", (int)len);

    // Try parsing after every chunk; reset only once a full JSON object is received.
    if (parse_json()) {
        ESP_LOGI(TAG, "Full JSON received and parsed.");
        json_index = 0;  // Reset buffer for next message
        memset(json_buffer, 0, MAX_JSON_SIZE);

        // Confirm provisioning to the app: notify the hostname on 0xFEF9. By now the
        // app has connected + subscribed, so (unlike the connect-time notify) this one
        // is reliably delivered. The app shows "provisioned ✓" on receipt instead of
        // having to infer success from the BLE disconnect.
        main_struct.isProvisioned = true;
        send_hostname();
    }

    return 0;
}



// Read data from ESP32 defined as server
static int device_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg){
    if (attr_handle == prov_status_attr_handle) {
        uint8_t provisioned_status = main_struct.isProvisioned;
        os_mbuf_append(ctxt->om, &provisioned_status, sizeof(provisioned_status));
        ESP_LOGI("BLE", "BLE Read: Provisioning Status - %u", provisioned_status);
    } 
    else if (attr_handle == hostname_attr_handle) {
        os_mbuf_append(ctxt->om, main_struct.hostname, strlen(main_struct.hostname));
        ESP_LOGI("BLE", "BLE Read: Hostname - %s", main_struct.hostname);
    } 
    else {
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
            {
                .uuid = BLE_UUID16_DECLARE(HOSTNAME_UUID),
                .access_cb = device_read,
                .val_handle = &hostname_attr_handle, // Assign handle
                // READ requires encryption: the app reads this first, which establishes
                // the encrypted link (pairing) BEFORE it writes config — no write retry.
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(JSON_CONFIG_UUID),
                // Require an encrypted link to write config (creds travel encrypted).
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
                .access_cb = device_write,
            },
            {0} // Terminating the characteristics array
        }
    },
    {0} // Terminating the services array
};

// Function to send a notification
static void send_hostname(){
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
static int ble_gap_event(struct ble_gap_event *event, void *arg){
    switch (event->type)
    {
    // Advertise if connected
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI("BLE", "Connection handle: %d", g_conn_handle);
            //ble_gattc_exchange_mtu(event->connect.conn_handle, MAX_MTU);
            get_wifi_mac_address();   // populate hostname so the encrypted READ returns it
            // No connect-time notify: the app READs 0xFEF9 (which pairs), then writes
            // config; the confirmation notify is sent from device_write after save.
        } else {
            ble_advert();
        }

        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECT (reason=%d)", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (main_struct.isProvisioned) {
            // Config was saved (set in device_write after parse_json). Reboot to join WiFi.
            ESP_LOGI("GAP", "Provisioned -> rebooting to join WiFi.");
            vTaskDelay(100);
            esp_restart();
        } else {
            // Disconnected BEFORE provisioning finished — e.g. a drop during BLE pairing.
            // Don't reboot (that would wipe progress and the LED would just keep
            // flashing); keep advertising so the app can reconnect and finish.
            ESP_LOGW("GAP", "Disconnected before provisioning -> re-advertising.");
            ble_app_advertise();
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        // Link encryption (pairing) completed or changed.
        ESP_LOGI("GAP", "Encryption change; status=%d", event->enc_change.status);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // The peer (phone) is bonded but we lost the keys (RAM store cleared by the
        // reboot after each provisioning). Delete the stale bond and let it re-pair so
        // re-provisioning always works.
        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
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

#define LED_GPIO 34   // V5: status LED (LED2) is on IO34. Was GPIO2 (V4/legacy).

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

static void nimble_host_task(void *param){
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
    // Release any RTC hold left on the pin by enter_deep_sleep() so the button is usable
    // while awake (the hold keeps IO3 pulled high across deep sleep for ext0 wake).
    rtc_gpio_hold_dis(BUTTON_GPIO);
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
void ble_app_on_sync(void){
    char *TAG = "BLE_SYNC";
    // The BLE stack is now synchronized and ready to run BLE operations
    ESP_LOGI(TAG, "BLE sync completed.");

    // Retrieve the MAC address
    uint8_t mac[6];   // Array to hold the MAC address (6 bytes)
    char device_name[32];  // Buffer to hold the MAC address as a string

    // Automatically infer the address type and MAC address
    //ble_hs_id_infer_auto(0, &ble_addr_type);  // Infers the address type
    //ble_hs_id_infer_auto(0, mac);  // Retrieves the MAC address

    ble_hs_id_infer_auto(0, &ble_addr_type);  // Get address type
    ble_hs_id_copy_addr(ble_addr_type, mac, NULL);  // Get MAC address


    // Convert only the last 4 bytes of the MAC address to string (without colons)
    mac_to_string(mac + 2, device_name);  // Only use last 4 bytes of MAC address

    // Create final device name with "PlantPulse-" prefix and the last 4 bytes of the MAC address
    char final_device_name[50];  // Ensure enough space for "PlantPulse-" + MAC address
    //snprintf(final_device_name, sizeof(final_device_name), "Plant Pulse %s", device_name);

    snprintf(final_device_name, sizeof(final_device_name), "Plant Pulse %.8s", device_name);


    // Set the BLE device name
    ble_svc_gap_device_name_set(final_device_name);  // Set the device name
    
    
    ESP_LOGI(TAG, "Device BLE name set to: %s", final_device_name);

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
    //ble_hs_cfg.attr_tab_cfg.max_attr = 64;  // Ensure enough attributes for GATT
    ble_att_set_preferred_mtu(256);  // Set before stack sync

    // BLE security: the config characteristic (0xFEFA) requires an ENCRYPTED link
    // (BLE_GATT_CHR_F_WRITE_ENC in gatt_svcs[]), so the WiFi password + api_token are
    // no longer written in the clear. The sensor has no display/keyboard, so use
    // "just works" pairing (no MITM protection, but it encrypts the link) with LE
    // Secure Connections + bonding. ble_store_config_init() provides key storage.
    ble_hs_cfg.sm_io_cap      = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding     = 1;
    ble_hs_cfg.sm_sc          = 1;   // LE Secure Connections
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    // Start NimBLE host task thread and return
    nimble_port_freertos_init(nimble_host_task);

//    xTaskCreate(nimble_host_task, "PlantPulse", 8 * 1024, NULL, 5, NULL);
    // Task entry log 
    ESP_LOGI(TAG, "NimBLE host task has been started!");

}


// Function to enter deep sleep based on the selected duration
void enter_deep_sleep(uint32_t seconds){
    static const char *TAG = "SLEEP";
    uint64_t sleep_duration_us = (uint64_t)seconds * (uint64_t)1000000; // Convert seconds to microseconds

    ESP_LOGI(TAG, "Entering deep sleep mode for %lu seconds...", (unsigned long)seconds);
    
    // Disable Wi-Fi before sleeping
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // Enable Wi-Fi power saving
    esp_wifi_stop(); // disable wifi driver

    // NOTE: Do NOT manually gpio_reset_pin() every GPIO here. The old loop iterated all
    // 48 pins, which reset the USB pins (IO19/20) and — fatally — the in-package SPI
    // flash pins, crashing the chip mid-loop before it ever slept. That was the real
    // cause of the ~10s reboot loop. ESP-IDF already isolates GPIOs in deep sleep
    // (sdkconfig "Configure to isolate all GPIO pins in sleep state"), so no manual
    // isolation is needed for leakage.

    // Configure the RTC timer to wake up after the specified sleep duration
    esp_sleep_enable_timer_wakeup(sleep_duration_us);

    // Wake on button press (SW1 on IO3, active low). IO3 has only a 100nF debounce cap
    // and no external pull-up, so enable the RTC pull-up and hold it across deep sleep so
    // the pin doesn't float low and wake us spuriously.
    rtc_gpio_pullup_en(BUTTON_GPIO);
    rtc_gpio_pulldown_dis(BUTTON_GPIO);
    rtc_gpio_hold_en(BUTTON_GPIO);
    esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);  // 0 = wake on active-low (button pressed)

    // Enter deep sleep
    esp_deep_sleep_start();
}


#include <string.h>
#include <stdlib.h>
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"   // trust the ESP-IDF root-CA bundle instead of pinning a cert

#define OTA_URL "https://athome.rodlandfarms.com/firmware.bin"
#define JSON_URL "https://athome.rodlandfarms.com/firmware.json"

static char current_version_number[] = "1781276957";  // provisioned-confirm notify on 0xFEF9 after config save

void perform_ota_update(void);  // forward declaration

// Versions are unix timestamps, so a newer build is numerically larger. Only update
// when the server version is STRICTLY greater than ours — this makes OTA monotonic
// (no accidental downgrade if an older firmware.json is ever served).
static void maybe_apply_update(const char *server_version) {
    char *TAG = "OTA_CHECK";
    long long sv  = strtoll(server_version, NULL, 10);
    long long cur = strtoll(current_version_number, NULL, 10);
    ESP_LOGI(TAG, "Server version: %s, current: %s", server_version, current_version_number);
    if (sv > cur) {
        ESP_LOGI(TAG, "Newer firmware available -> starting OTA");
        perform_ota_update();
    } else {
        ESP_LOGI(TAG, "No update required (server <= current).");
    }
}

void perform_ota_update(){
    char *TAG = "OTA_UPDATE";
    ESP_LOGI(TAG, "Starting OTA update...");
    
    esp_http_client_config_t config = {
        .url = OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA update successful! Restarting...");
        esp_restart();
    }
    else
    {
        ESP_LOGE(TAG, "OTA update failed!");
    }
}

void check_update(void *pvParameters) {  
    esp_log_level_set("*", ESP_LOG_DEBUG);
    char *TAG = "OTA_CHECK";
    ESP_LOGI(TAG, "Checking for firmware updates...");

    esp_http_client_config_t http_config = {
        .url = JSON_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 3000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
         
        ESP_LOGI(TAG, "HTTP Response Code: %d", status_code); 

        // Read firmware.json with esp_http_client_read into a small fixed buffer (the
        // file is tiny). Works for chunked and non-chunked responses and replaces the
        // old buggy chunked branch (it read into an unallocated pointer and memcpy'd a
        // buffer onto itself).
        char buffer[256] = {0};
        int total_read = 0, bytes_read = 0;
        while (total_read < (int)sizeof(buffer) - 1 &&
               (bytes_read = esp_http_client_read(client, buffer + total_read,
                                                  sizeof(buffer) - 1 - total_read)) > 0) {
            total_read += bytes_read;
        }
        buffer[total_read] = '\0';

        if (total_read > 0) {
            ESP_LOGI(TAG, "Received JSON: %s", buffer);
            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
                if (version && cJSON_IsString(version) && version->valuestring) {
                    maybe_apply_update(version->valuestring);
                } else {
                    ESP_LOGE(TAG, "firmware.json missing string 'version'");
                }
                cJSON_Delete(json);
            } else {
                ESP_LOGE(TAG, "JSON Parsing Error");
            }
        } else {
            ESP_LOGE(TAG, "Empty firmware.json response");
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "check_update done.");
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