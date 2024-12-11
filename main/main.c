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

#define SSID_CHR_UUID 0xFEF4
#define PASS_CHR_UUID 0xFEF5
#define PROV_STATUS_UUID 0xDEAD
#define MANUFACTURER_NAME "Rodland Farms"



char *TAG = "PlantPulse";

static bool ssid_pswd_flag[2] = {
    false,
};
uint8_t ble_addr_type;
uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t prov_status_attr_handle = 0;

main_struct_t main_struct = {.credentials_recv = 0, .isProvisioned = false};
void ble_app_advertise(void);

// Notification function for PROV_STATUS_UUID
void notify_prov_status(uint8_t status_data)
{
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

// Write data to ESP32 defined as server
static int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ble_uuid_u16(ctxt->chr->uuid))
    {
    case SSID_CHR_UUID:
        ssid_pswd_flag[0] = true;
        strncpy(main_struct.ssid, (char *)ctxt->om->om_data, ctxt->om->om_len);
        main_struct.ssid[ctxt->om->om_len] = '\0';
        ESP_LOGI(TAG, "Received SSID: %s", main_struct.ssid);
        break;

    case PASS_CHR_UUID:
        ssid_pswd_flag[1] = true;
        strncpy(main_struct.password, (char *)ctxt->om->om_data, ctxt->om->om_len);
        main_struct.password[ctxt->om->om_len] = '\0';
        ESP_LOGI(TAG, "Received Password: %s", main_struct.password);
        break;

    default:
        ESP_LOGW(TAG, "Unknown characteristic written.");
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ssid_pswd_flag[0] && ssid_pswd_flag[1]) // ssid and password received
    {

        main_struct.credentials_recv = true;

        save_to_nvs(main_struct.ssid, main_struct.password, main_struct.credentials_recv);
        printf("Password saved to %s\n", main_struct.password);
        printf("ssid to %s\n", main_struct.ssid);
        printf("wvalue %d\n", main_struct.credentials_recv);
    }

    return 0;
}

// Read data from ESP32 defined as server
static int device_read(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // uint32_t status_data = main_struct.isProvisioned ? 1 : 0; // Example: 1 = provisioned, 0 = not provisioned
    // os_mbuf_append(ctxt->om, &status_data, sizeof(status_data));
    // ESP_LOGI(TAG, "BLE Read: Provisioning Status - %u", status_data);
    return 0; // Success
}


// Array of pointers to other service definitions
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {// service
     .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180), // Define UUID for device type
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(SSID_CHR_UUID), // Define UUID for writing ssid
          .flags = BLE_GATT_CHR_F_WRITE,
          .access_cb = device_write},
         {.uuid = BLE_UUID16_DECLARE(PASS_CHR_UUID), // Define UUID for writing pswd
          .flags = BLE_GATT_CHR_F_WRITE,
          .access_cb = device_write},
         {.uuid = BLE_UUID16_DECLARE(PROV_STATUS_UUID), // Define UUID for reading
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
          .access_cb = device_read,
          .val_handle = &prov_status_attr_handle},
         {0}}},
    {0} // No more service
};

// BLE event handling
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    // Advertise if connected
    case BLE_GAP_EVENT_CONNECT:

        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status == 0)
            g_conn_handle = event->connect.conn_handle;
        else if (event->connect.status != 0)
            ble_app_advertise();

        break;
    case BLE_GAP_EVENT_DISCONNECT:

        break;
    // Advertise again after completion of the event/advertisement
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI("GAP", "BLE GAP EVENT");
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0;
}

// Define the BLE connection
void ble_app_advertise(void)
{
    // GAP - device name definition
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name(); // Read the BLE device name
    uint8_t manuf_data[12]; // Ensure the total advertisement data doesn't exceed 31 bytes
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // GAP - device connectivity definition
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // connectable or non-connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // discoverable or non-discoverable
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

// The application
void ble_app_on_sync(void)
{
    ble_hs_id_infer_auto(0, &ble_addr_type); // Determines the best address type automatically
    ble_app_advertise();                     // Define the BLE connection
}

static void nimble_host_task(void *param)
{
    // Task entry log 
    ESP_LOGI(TAG, "NimBLE host task has been started!");

    // This function won't return until nimble_port_stop() is executed
    nimble_port_run();

    // Clean up at exit 
    vTaskDelete(NULL);
}

void disable_ble(void)
{
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
    while (1)
    {
        //if (main_struct.credentials_recv)
        //{
            ESP_LOGI(TAG, "Checking WiFi Credentials");
            wifi_init();
            //disable_ble();
            vTaskDelete(NULL);
        //}

        vTaskDelay(1000);
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

void notify_status(void *arg)
{
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

// Main application entry point
void app_main() {

    // Configure ADC1 channel 5 (GPIO5) for 12-bit width and 11dB attenuation
    adc1_config_width(ADC_WIDTH_BIT_12);  // Set ADC width to 12 bits (0-4095 range)
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);  // Set ADC attenuation to 11dB (0-3.6V range)


    
    nvs_init();
    read_from_nvs(main_struct.ssid, main_struct.password, &main_struct.credentials_recv);

    // Only initialize BLE if credentials are NOT set
    if (!main_struct.credentials_recv) {
        // Initialize NimBLE host and start advertising for provisioning
        nimble_port_init();                        // 3 - Initialize the host stack
        ble_svc_gap_device_name_set("PlantPulse"); // 4 - Set BLE device name
        ble_svc_gap_init();                        // 4 - Initialize NimBLE GAP service
        ble_svc_gatt_init();                       // 4 - Initialize NimBLE GATT service
        ble_gatts_count_cfg(gatt_svcs);            // 4 - Initialize NimBLE config GATT services
        ble_gatts_add_svcs(gatt_svcs);             // 4 - Add GATT services
        ble_hs_cfg.sync_cb = ble_app_on_sync;      // 5 - Initialize the application

        // Start NimBLE host task thread and return 
        xTaskCreate(nimble_host_task, "PlantPulse", 4 * 1024, NULL, 5, NULL);
        // Task entry log 
        ESP_LOGI(TAG, "NimBLE host task has been started!");

    } else {
        ESP_LOGI(TAG, "Wi-Fi credentials already set. Skipping BLE provisioning.");
    } 

    // Start moisture reading in a separate FreeRTOS task
    //xTaskCreate(monitor, "monitor", 4 * 1024, NULL, 5, NULL);
    // Check Wifi Credentials 
    xTaskCreate(check_credentials, "check_credentials", 4 * 1024, NULL, 5, NULL);

    //xTaskCreate(notify_status, "notify_status", 2 * 1024, NULL, 5, NULL);
}