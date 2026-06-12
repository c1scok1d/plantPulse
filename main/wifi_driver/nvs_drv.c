#include <stdint.h>
#include <esp_err.h>
#include "esp_log.h"
#include "nvs_drv.h"
#include "nvs_flash.h"
#include "nvs.h"

esp_err_t save_to_nvs(const char *ssid, const char *password, char *name, char *location, char *apiToken, uint8_t value){
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS handle
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    // Save SSID
    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write SSID!");
        nvs_close(nvs_handle);
        return err;
    }
    printf("NVS stored ssid %s\n", ssid);

    // Save Password
    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write Password!");
        nvs_close(nvs_handle);
        return err;
    }
    printf("NVS stored password %s\n", password);

    // Save Name
    err = nvs_set_str(nvs_handle, "name", name);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write device name!");
        nvs_close(nvs_handle);
        return err;
    }
    printf("NVS stored name %s\n", name);

    // Save Location
    err = nvs_set_str(nvs_handle, "location", location);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write device location!");
        nvs_close(nvs_handle);
        return err;
    }
    printf("NVS stored location %s\n", location);

    // Save API Token
    err = nvs_set_str(nvs_handle, "apiToken", apiToken);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write API Token!");
        nvs_close(nvs_handle);
        return err;
    }
    printf("NVS stored apiToken %s\n", apiToken);


    // Save uint8_t value
    err = nvs_set_u8(nvs_handle, "wifi_value", value);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write uint8_t!");
        nvs_close(nvs_handle);
        return err;
    }
    printf("NVS stored wifi_value %d\n", value);


    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to commit changes!");
    }

    // Close the handle
    nvs_close(nvs_handle);


    return err;
}

uint32_t nvs_get_sleep_seconds(void) {
    nvs_handle_t nvs_handle;
    uint32_t secs = DEFAULT_SLEEP_SECONDS;
    if (nvs_open("storage", NVS_READONLY, &nvs_handle) != ESP_OK) {
        return secs;  // not provisioned yet -> default
    }
    uint32_t stored = 0;
    if (nvs_get_u32(nvs_handle, "sleep_secs", &stored) == ESP_OK && stored > 0) {
        secs = stored;
    }
    nvs_close(nvs_handle);
    return secs;
}

esp_err_t nvs_set_sleep_seconds(uint32_t seconds) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error (%s) opening NVS for sleep_secs!", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u32(nvs_handle, "sleep_secs", seconds);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        printf("NVS stored sleep_secs %lu\n", (unsigned long)seconds);
    }
    nvs_close(nvs_handle);
    return err;
}

esp_err_t read_from_nvs(char *ssid, char *password, char *name, char *location, char *apiToken, uint8_t *value)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t ssid_len = 32;
    size_t pass_len = 64;
    size_t name_len = 32;
    size_t location_len = 64;
    size_t apiToken_len = 64;
    // Open NVS handle
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    // Read SSID

    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "SSID not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "ssid", "");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default SSID!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read SSID!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read Password
    
    err = nvs_get_str(nvs_handle, "password", password, &pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Password not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "password", "");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default password!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read Password!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read Name
    
    err = nvs_get_str(nvs_handle, "name", name, &name_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Device name not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "name", "Set Device Name");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default device name!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read device name!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read Location
    
    err = nvs_get_str(nvs_handle, "location", location, &location_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Device location not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "location", "Set Device Location");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default device location!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read device loction!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read API Token
    
    err = nvs_get_str(nvs_handle, "apiToken", apiToken, &apiToken_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Device API token not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "apiToken", "Set Device API Token");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default device API Token!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read device loction!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read uint8_t value
    
    err = nvs_get_u8(nvs_handle, "wifi_value", value);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "wifi_value not found. Setting default value.");
        err = nvs_set_u8(nvs_handle, "wifi_value", 0);
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default uint8_t value!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read uint8_t!");
        nvs_close(nvs_handle);
        return err;
    }

    // Commit any changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to commit changes!");
        nvs_close(nvs_handle);
        return err;
    }
    // Close the handle
    nvs_close(nvs_handle);

    return ESP_OK;
}
